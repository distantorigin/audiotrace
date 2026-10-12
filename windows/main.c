#define COBJMACROS
#define CONST_VTABLE
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include "audio_meter_compat.h"
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include "../common/audio_events.h"

#define WM_TRAY (WM_APP + 1)
#define WM_REFRESH (WM_APP + 2)
#define ID_PAUSE 100
#define ID_CLEAR 101
#define ID_COPY 102
#define ID_QUIT 103
#define ID_EVENT 200

typedef struct Endpoint Endpoint;
typedef struct SessionNotice SessionNotice;
typedef struct Session {
    wchar_t *key;
    IAudioSessionControl2 *control;
    IAudioMeterInformation *meter;
    ISimpleAudioVolume *volume;
    ATDetector detector;
    double lastSample;
    ATEvent metadata;
    Endpoint *endpoint;
    struct Session *next;
} Session;

struct Endpoint {
    wchar_t *id;
    IAudioSessionManager2 *manager;
    char name[256];
    BOOL seen;
    BOOL invalidated;
    SessionNotice *notice;
    struct Endpoint *next;
};

/* Core Audio calls these from COM threads; only post work to the UI thread. */
struct SessionNotice { IAudioSessionNotification iface; LONG refs; HWND window; wchar_t *deviceID; };
typedef struct { IMMNotificationClient iface; LONG refs; HWND window; } DeviceNotice;
typedef struct PendingSession {
    IAudioSessionControl *control;
    wchar_t *deviceID;
    struct PendingSession *next;
} PendingSession;

static struct {
    HWND window;
    IMMDeviceEnumerator *enumerator;
    DeviceNotice deviceNotice;
    SRWLOCK pendingLock;
    PendingSession *pending;
    BOOL closing;
    Endpoint *endpoints;
    Session *sessions;
    ATHistory history;
    BOOL paused;
    BOOL trayAdded;
    BOOL deviceNoticeRegistered;
    HICON icon;
    UINT taskbarCreated;
    HRESULT lastError;
    LARGE_INTEGER frequency;
} app;

static HRESULT STDMETHODCALLTYPE session_query(IAudioSessionNotification *self, REFIID iid, void **out) {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IAudioSessionNotification)) return E_NOINTERFACE;
    *out = self;
    InterlockedIncrement(&((SessionNotice *)self)->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE session_addref(IAudioSessionNotification *self) {
    return (ULONG)InterlockedIncrement(&((SessionNotice *)self)->refs);
}
static ULONG STDMETHODCALLTYPE session_release(IAudioSessionNotification *self) {
    SessionNotice *notice = (SessionNotice *)self;
    ULONG refs = (ULONG)InterlockedDecrement(&notice->refs);
    if (!refs) { free(notice->deviceID); free(notice); }
    return refs;
}
static HRESULT STDMETHODCALLTYPE session_created(IAudioSessionNotification *self, IAudioSessionControl *session) {
    SessionNotice *notice = (SessionNotice *)self;
    PendingSession *pending = calloc(1, sizeof(*pending));
    if (!pending) return E_OUTOFMEMORY;
    pending->deviceID = _wcsdup(notice->deviceID);
    if (!pending->deviceID) { free(pending); return E_OUTOFMEMORY; }
    IAudioSessionControl_AddRef(session);
    pending->control = session;
    AcquireSRWLockExclusive(&app.pendingLock);
    if (app.closing) {
        ReleaseSRWLockExclusive(&app.pendingLock);
        IAudioSessionControl_Release(session); free(pending->deviceID); free(pending);
        return S_OK;
    }
    pending->next = app.pending;
    app.pending = pending;
    ReleaseSRWLockExclusive(&app.pendingLock);
    PostMessageW(notice->window, WM_REFRESH, 0, 0);
    return S_OK;
}
static const IAudioSessionNotificationVtbl session_vtable = {
    session_query, session_addref, session_release, session_created
};

static HRESULT STDMETHODCALLTYPE device_query(IMMNotificationClient *self, REFIID iid, void **out) {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IMMNotificationClient)) return E_NOINTERFACE;
    *out = self;
    InterlockedIncrement(&((DeviceNotice *)self)->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE device_addref(IMMNotificationClient *self) {
    return (ULONG)InterlockedIncrement(&((DeviceNotice *)self)->refs);
}
static ULONG STDMETHODCALLTYPE device_release(IMMNotificationClient *self) {
    return (ULONG)InterlockedDecrement(&((DeviceNotice *)self)->refs);
}
static HRESULT STDMETHODCALLTYPE device_state(IMMNotificationClient *self, LPCWSTR id, DWORD state) {
    (void)id; (void)state;
    PostMessageW(((DeviceNotice *)self)->window, WM_REFRESH, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE device_added(IMMNotificationClient *self, LPCWSTR id) {
    return device_state(self, id, 0);
}
static HRESULT STDMETHODCALLTYPE device_removed(IMMNotificationClient *self, LPCWSTR id) {
    return device_state(self, id, 0);
}
static HRESULT STDMETHODCALLTYPE device_default(IMMNotificationClient *self, EDataFlow flow, ERole role, LPCWSTR id) {
    (void)flow; (void)role;
    return device_state(self, id, 0);
}
static HRESULT STDMETHODCALLTYPE device_property(IMMNotificationClient *self, LPCWSTR id, PROPERTYKEY key) {
    (void)key;
    return device_state(self, id, 0);
}
static const IMMNotificationClientVtbl device_vtable = {
    device_query, device_addref, device_release, device_state, device_added,
    device_removed, device_default, device_property
};

static double monotonic_time(void) {
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return (double)ticks.QuadPart / app.frequency.QuadPart;
}
static double wall_time(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ticks;
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;
    return ticks.QuadPart / 1e7 - 11644473600.0;
}
static void to_utf8(const wchar_t *input, char *output, size_t capacity) {
    if (!input) { output[0] = 0; return; }
    if (!WideCharToMultiByte(CP_UTF8, 0, input, -1, output, (int)capacity, NULL, NULL)) {
        /* Avoid cutting a multibyte character when truncating a long name. */
        size_t length = wcslen(input);
        while (length && !WideCharToMultiByte(CP_UTF8, 0, input, (int)length,
                                             output, (int)capacity - 1, NULL, NULL)) --length;
        int size = WideCharToMultiByte(CP_UTF8, 0, input, (int)length,
                                      output, (int)capacity - 1, NULL, NULL);
        output[size > 0 ? size : 0] = 0;
    }
}
static void to_wide(const char *input, wchar_t *output, size_t capacity) {
    if (!MultiByteToWideChar(CP_UTF8, 0, input, -1, output, (int)capacity)) output[0] = 0;
}
static wchar_t *session_key(const wchar_t *device, const wchar_t *instance) {
    size_t capacity = wcslen(device) + wcslen(instance) + 2;
    wchar_t *key = calloc(capacity, sizeof(wchar_t));
    if (key) swprintf(key, capacity, L"%ls|%ls", device, instance);
    return key;
}

static void free_session(Session *session) {
    if (session->volume) ISimpleAudioVolume_Release(session->volume);
    if (session->meter) IAudioMeterInformation_Release(session->meter);
    if (session->control) IAudioSessionControl2_Release(session->control);
    free(session->key);
    free(session);
}
static void free_endpoint(Endpoint *endpoint) {
    if (endpoint->notice)
        IAudioSessionManager2_UnregisterSessionNotification(endpoint->manager, &endpoint->notice->iface);
    IAudioSessionManager2_Release(endpoint->manager);
    if (endpoint->notice) session_release(&endpoint->notice->iface);
    free(endpoint->id);
    free(endpoint);
}
static void clear_sources(void) {
    while (app.sessions) {
        Session *next = app.sessions->next;
        free_session(app.sessions);
        app.sessions = next;
    }
    while (app.endpoints) {
        Endpoint *next = app.endpoints->next;
        free_endpoint(app.endpoints);
        app.endpoints = next;
    }
}

static void describe_session(Session *session, Endpoint *endpoint, DWORD pid, BOOL shared) {
    session->metadata.pid = pid;
    wchar_t path[1024] = {0};
    DWORD capacity = sizeof(path) / sizeof(path[0]);
    HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL;
    if (process) {
        if (!QueryFullProcessImageNameW(process, 0, path, &capacity)) path[0] = 0;
        CloseHandle(process);
    }
    if (IAudioSessionControl2_IsSystemSoundsSession(session->control) == S_OK) {
        strcpy(session->metadata.source, "System Sounds");
    } else if (path[0]) {
        const wchar_t *name = wcsrchr(path, L'\\');
        to_utf8(name ? name + 1 : path, session->metadata.source, sizeof(session->metadata.source));
    } else {
        LPWSTR display = NULL;
        IAudioSessionControl2_GetDisplayName(session->control, &display);
        if (display && display[0] && display[0] != L'@')
            to_utf8(display, session->metadata.source, sizeof(session->metadata.source));
        else snprintf(session->metadata.source, sizeof(session->metadata.source), "Process %lu", (unsigned long)pid);
        CoTaskMemFree(display);
    }
    char utf8path[1024];
    to_utf8(path, utf8path, sizeof(utf8path));
    snprintf(session->metadata.detail, sizeof(session->metadata.detail),
             "PID %lu%s | %.180s%s%.240s", (unsigned long)pid,
             shared ? " (shared session; creator PID)" : "", endpoint->name,
             utf8path[0] ? " | " : "", utf8path);
}

static void add_session(Endpoint *endpoint, IAudioSessionControl *base) {
        IAudioSessionControl2 *control = NULL;
        LPWSTR instance = NULL;
        wchar_t *key = NULL;
        HRESULT hr = IAudioSessionControl_QueryInterface(base, &IID_IAudioSessionControl2, (void **)&control);
        if (FAILED(hr)) return;
        AudioSessionState state;
        if (FAILED(IAudioSessionControl2_GetState(control, &state)) || state == AudioSessionStateExpired) goto done;
        if (FAILED(IAudioSessionControl2_GetSessionInstanceIdentifier(control, &instance))) goto done;
        key = session_key(endpoint->id, instance);
        if (!key) { app.lastError = E_OUTOFMEMORY; goto done; }
        Session *existing = app.sessions;
        while (existing && (wcscmp(existing->key, key) || existing->endpoint->invalidated))
            existing = existing->next;
        if (existing) goto done;
        DWORD pid = 0;
        hr = IAudioSessionControl2_GetProcessId(control, &pid);
        if (FAILED(hr) || pid == GetCurrentProcessId()) goto done;
        Session *session = calloc(1, sizeof(*session));
        if (!session) { app.lastError = E_OUTOFMEMORY; goto done; }
        HRESULT meterHR = IAudioSessionControl2_QueryInterface(control, &IID_IAudioMeterInformation, (void **)&session->meter);
        if (FAILED(meterHR)) { app.lastError = meterHR; free(session); goto done; }
        IAudioSessionControl2_QueryInterface(control, &IID_ISimpleAudioVolume, (void **)&session->volume);
        session->key = key; key = NULL;
        session->control = control; control = NULL;
        session->endpoint = endpoint;
        describe_session(session, endpoint, pid, hr != S_OK);
        session->next = app.sessions;
        app.sessions = session;
done:
        free(key);
        CoTaskMemFree(instance);
        if (control) IAudioSessionControl2_Release(control);
}

static BOOL audio_connection_lost(HRESULT hr) {
    return hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING ||
           hr == RPC_E_DISCONNECTED || hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE);
}

static void discover_sessions(Endpoint *endpoint) {
    IAudioSessionEnumerator *enumerator = NULL;
    HRESULT hr = IAudioSessionManager2_GetSessionEnumerator(endpoint->manager, &enumerator);
    if (FAILED(hr)) {
        app.lastError = hr;
        if (audio_connection_lost(hr))
            endpoint->invalidated = TRUE, endpoint->seen = FALSE;
        return;
    }
    int count = 0;
    hr = IAudioSessionEnumerator_GetCount(enumerator, &count);
    if (FAILED(hr)) {
        app.lastError = hr;
        if (audio_connection_lost(hr)) endpoint->invalidated = TRUE, endpoint->seen = FALSE;
        IAudioSessionEnumerator_Release(enumerator);
        return;
    }
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl *base = NULL;
        if (SUCCEEDED(IAudioSessionEnumerator_GetSession(enumerator, i, &base))) {
            add_session(endpoint, base);
            IAudioSessionControl_Release(base);
        }
    }
    IAudioSessionEnumerator_Release(enumerator);
}

static void drain_pending_sessions(BOOL discard) {
    AcquireSRWLockExclusive(&app.pendingLock);
    PendingSession *pending = app.pending;
    app.pending = NULL;
    ReleaseSRWLockExclusive(&app.pendingLock);
    while (pending) {
        PendingSession *next = pending->next;
        if (!discard && !app.paused) {
            Endpoint *endpoint = app.endpoints;
            while (endpoint && wcscmp(endpoint->id, pending->deviceID)) endpoint = endpoint->next;
            if (endpoint) add_session(endpoint, pending->control);
        }
        IAudioSessionControl_Release(pending->control);
        free(pending->deviceID); free(pending);
        pending = next;
    }
}

static void refresh_sources(void) {
    if (app.paused || !app.enumerator) return;
    IMMDeviceCollection *devices = NULL;
    HRESULT hr = IMMDeviceEnumerator_EnumAudioEndpoints(app.enumerator, eRender, DEVICE_STATE_ACTIVE, &devices);
    if (FAILED(hr)) { app.lastError = hr; return; }
    UINT count = 0;
    hr = IMMDeviceCollection_GetCount(devices, &count);
    if (FAILED(hr)) { app.lastError = hr; IMMDeviceCollection_Release(devices); return; }
    app.lastError = S_OK;
    BOOL complete = TRUE;
    for (Endpoint *e = app.endpoints; e; e = e->next) e->seen = FALSE;
    for (UINT i = 0; i < count; ++i) {
        IMMDevice *device = NULL;
        LPWSTR id = NULL;
        hr = IMMDeviceCollection_Item(devices, i, &device);
        if (FAILED(hr)) { complete = FALSE; app.lastError = hr; continue; }
        hr = IMMDevice_GetId(device, &id);
        if (FAILED(hr)) { complete = FALSE; app.lastError = hr; IMMDevice_Release(device); continue; }
        Endpoint *endpoint = app.endpoints;
        while (endpoint && (wcscmp(endpoint->id, id) || endpoint->invalidated)) endpoint = endpoint->next;
        if (!endpoint) {
            endpoint = calloc(1, sizeof(*endpoint));
            if (!endpoint) { app.lastError = E_OUTOFMEMORY; goto next_device; }
            hr = IMMDevice_Activate(device, &IID_IAudioSessionManager2, CLSCTX_ALL, NULL, (void **)&endpoint->manager);
            if (FAILED(hr)) { app.lastError = hr; free(endpoint); goto next_device; }
            endpoint->id = _wcsdup(id);
            if (!endpoint->id) {
                IAudioSessionManager2_Release(endpoint->manager);
                free(endpoint); app.lastError = E_OUTOFMEMORY; goto next_device;
            }
            strcpy(endpoint->name, "Playback device");
            IPropertyStore *properties = NULL;
            if (SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &properties))) {
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(IPropertyStore_GetValue(properties, &PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                    to_utf8(value.pwszVal, endpoint->name, sizeof(endpoint->name));
                PropVariantClear(&value);
                IPropertyStore_Release(properties);
            }
            endpoint->notice = calloc(1, sizeof(*endpoint->notice));
            if (endpoint->notice) {
                endpoint->notice->iface.lpVtbl = &session_vtable;
                endpoint->notice->refs = 1;
                endpoint->notice->window = app.window;
                endpoint->notice->deviceID = _wcsdup(id);
                if (endpoint->notice->deviceID) {
                    hr = IAudioSessionManager2_RegisterSessionNotification(endpoint->manager, &endpoint->notice->iface);
                    if (FAILED(hr)) app.lastError = hr;
                } else app.lastError = E_OUTOFMEMORY;
            } else app.lastError = E_OUTOFMEMORY;
            endpoint->next = app.endpoints;
            app.endpoints = endpoint;
        }
        endpoint->seen = TRUE;
        discover_sessions(endpoint);
next_device:
        CoTaskMemFree(id);
        IMMDevice_Release(device);
    }
    IMMDeviceCollection_Release(devices);
    if (!complete) {
        /* An incomplete device list cannot prove an existing endpoint was removed. */
        for (Endpoint *e = app.endpoints; e; e = e->next) e->seen = TRUE;
    }
    Session **session = &app.sessions;
    while (*session) {
        AudioSessionState state = AudioSessionStateInactive;
        HRESULT stateHR = IAudioSessionControl2_GetState((*session)->control, &state);
        if (audio_connection_lost(stateHR)) (*session)->endpoint->invalidated = TRUE;
        /* Preserve our own list even if an enumerator omits a notified session
           or transiently fails. Only retired devices/sessions reset detectors. */
        if (!(*session)->endpoint->seen || state == AudioSessionStateExpired ||
            audio_connection_lost(stateHR)) {
            Session *old = *session; *session = old->next; free_session(old);
        } else session = &(*session)->next;
    }
    Endpoint **endpoint = &app.endpoints;
    while (*endpoint) {
        if (!(*endpoint)->seen) {
            Endpoint *old = *endpoint; *endpoint = old->next; free_endpoint(old);
        } else endpoint = &(*endpoint)->next;
    }
    drain_pending_sessions(FALSE);
}

static void sample_sessions(void) {
    if (app.paused) return;
    for (Session *s = app.sessions; s; s = s->next) {
        float peak = 0;
        HRESULT hr = IAudioMeterInformation_GetPeakValue(s->meter, &peak);
        if (FAILED(hr)) {
            app.lastError = hr;
            if (audio_connection_lost(hr))
                s->endpoint->invalidated = TRUE, s->endpoint->seen = FALSE;
            continue;
        }
        BOOL muted = FALSE;
        if (s->volume && SUCCEEDED(ISimpleAudioVolume_GetMute(s->volume, &muted)) && muted) peak = 0;
        double now = monotonic_time();
        /* Missing polls do not establish silence between two loud samples. */
        if (s->lastSample && s->detector.active && now - s->lastSample >= AT_SILENCE_SECONDS)
            s->detector.last_above += now - s->lastSample;
        s->lastSample = now;
        if (at_detector_observe(&s->detector, peak, now)) {
            ATEvent event = s->metadata;
            event.timestamp = wall_time(); event.order = now; event.peak = peak;
            at_history_push(&app.history, &event);
        }
    }
}

static void event_line(const ATEvent *event, wchar_t *text, size_t capacity, BOOL details) {
    ULARGE_INTEGER ticks;
    ticks.QuadPart = (ULONGLONG)((event->timestamp + 11644473600.0) * 1e7);
    FILETIME utc = {ticks.LowPart, ticks.HighPart}, local;
    SYSTEMTIME time = {0};
    FileTimeToLocalFileTime(&utc, &local);
    FileTimeToSystemTime(&local, &time);
    wchar_t source[256], detail[512];
    to_wide(event->source, source, sizeof(source) / sizeof(source[0]));
    to_wide(event->detail, detail, sizeof(detail) / sizeof(detail[0]));
    swprintf(text, capacity, details ? L"%02u:%02u:%02u  %ls\t%ls\r\n" : L"%02u:%02u:%02u  %ls",
             time.wHour, time.wMinute, time.wSecond, source, detail);
}

static void copy_history(void) {
    wchar_t text[10000] = {0};
    size_t used = 0;
    for (size_t i = 0; i < app.history.count; ++i) {
        event_line(&app.history.events[i], text + used, 10000 - used, TRUE);
        used = wcslen(text);
    }
    if (!OpenClipboard(app.window)) return;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (used + 1) * sizeof(wchar_t));
    if (memory) {
        void *data = GlobalLock(memory);
        if (data) {
            memcpy(data, text, (used + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else GlobalFree(memory);
    }
    CloseClipboard();
}

static void add_tray(void) {
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof(data); data.hWnd = app.window; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = WM_TRAY; data.hIcon = app.icon;
    wcscpy(data.szTip, L"AudioTrace");
    app.trayAdded = Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    if (app.trayAdded) Shell_NotifyIconW(NIM_SETVERSION, &data);
}

static void show_menu(void) {
    sample_sessions();
    HMENU menu = CreatePopupMenu();
    wchar_t status[256];
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, app.paused ? L"Paused" : L"Monitoring");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    if (!app.history.count) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No sounds detected yet");
    for (size_t i = 0; i < app.history.count; ++i) {
        wchar_t line[512];
        event_line(&app.history.events[i], line, 512, FALSE);
        /* Escape menu mnemonics so an app named A&B displays correctly. */
        wchar_t escaped[1024];
        size_t j = 0;
        for (size_t k = 0; line[k] && j + 2 < 1024; ++k) {
            if (line[k] == L'&') escaped[j++] = L'&';
            escaped[j++] = line[k];
        }
        escaped[j] = 0;
        AppendMenuW(menu, MF_STRING, ID_EVENT + i, escaped);
    }
    if (FAILED(app.lastError)) {
        swprintf(status, 256, L"Some sources unavailable (0x%08lX)", (unsigned long)app.lastError);
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_PAUSE, app.paused ? L"Resume monitoring" : L"Pause monitoring");
    AppendMenuW(menu, MF_STRING | (app.history.count ? 0 : MF_GRAYED), ID_COPY, L"Copy history");
    AppendMenuW(menu, MF_STRING, ID_CLEAR, L"Clear history");
    AppendMenuW(menu, MF_STRING, ID_QUIT, L"Quit AudioTrace");
    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(app.window);
    /* Snapshot indices while TrackPopupMenu pumps timers/COM notification messages. */
    ATHistory snapshot = app.history;
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                  point.x, point.y, 0, app.window, NULL);
    DestroyMenu(menu);
    PostMessageW(app.window, WM_NULL, 0, 0);
    if (command == ID_QUIT) DestroyWindow(app.window);
    else if (command == ID_CLEAR) at_history_clear(&app.history);
    else if (command == ID_COPY) copy_history();
    else if (command == ID_PAUSE) {
        app.paused = !app.paused;
        if (app.paused) clear_sources(); else { refresh_sources(); sample_sessions(); }
    } else if (command >= ID_EVENT && command < ID_EVENT + snapshot.count) {
        ATEvent *event = &snapshot.events[command - ID_EVENT];
        wchar_t detail[1024], name[256], message[1400];
        to_wide(event->detail, detail, 1024);
        to_wide(event->source, name, 256);
        swprintf(message, 1400, L"%ls\n%ls\nPeak at onset: %.1f dBFS", name, detail,
                 20 * log10(fmax(event->peak, 1e-9f)));
        MessageBoxW(app.window, message, L"AudioTrace — sound source", MB_OK | MB_ICONINFORMATION);
    }
}

static HICON create_icon(void) {
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 32; info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD *pixels = NULL;
    HBITMAP color = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (!color) return NULL;
    const int heights[] = {5, 9, 14, 7, 11};
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        int dx = x - 16, dy = y - 16;
        DWORD pixel = dx * dx + dy * dy < 240 ? 0xff2775df : 0;
        for (int bar = 0; bar < 5; ++bar)
            if (x >= 6 + bar * 4 && x <= 7 + bar * 4 && abs(dy) <= heights[bar] / 2) pixel = 0xffffffff;
        pixels[y * 32 + x] = pixel;
    }
    BYTE maskBits[128] = {0};
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, maskBits);
    ICONINFO icon = {0}; icon.fIcon = TRUE; icon.hbmColor = color; icon.hbmMask = mask;
    HICON result = mask ? CreateIconIndirect(&icon) : NULL;
    if (mask) DeleteObject(mask);
    DeleteObject(color);
    return result;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == app.taskbarCreated && app.taskbarCreated) { add_tray(); return 0; }
    switch (message) {
        case WM_TRAY:
            if (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == NIN_SELECT || LOWORD(lp) == NIN_KEYSELECT)
                show_menu();
            return 0;
        case WM_REFRESH:
            refresh_sources(); drain_pending_sessions(FALSE); sample_sessions(); return 0;
        case WM_TIMER:
            if (wp == 1) sample_sessions(); else refresh_sources();
            return 0;
        case WM_DESTROY: {
            KillTimer(window, 1); KillTimer(window, 2);
            AcquireSRWLockExclusive(&app.pendingLock);
            app.closing = TRUE;
            ReleaseSRWLockExclusive(&app.pendingLock);
            if (app.deviceNoticeRegistered)
                IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(app.enumerator, &app.deviceNotice.iface);
            clear_sources();
            drain_pending_sessions(TRUE);
            NOTIFYICONDATAW data = {0}; data.cbSize = sizeof(data); data.hWnd = window; data.uID = 1;
            if (app.trayAdded) Shell_NotifyIconW(NIM_DELETE, &data);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous; (void)show;
    BOOL diagnostic = wcscmp(command, L"--diagnose") == 0;
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\AudioTraceTray");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS && !diagnostic) { CloseHandle(mutex); return 0; }
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { CloseHandle(mutex); return 1; }
    QueryPerformanceFrequency(&app.frequency);
    app.deviceNotice.iface.lpVtbl = &device_vtable; app.deviceNotice.refs = 1;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&app.enumerator);
    if (FAILED(hr)) {
        wchar_t text[256];
        swprintf(text, 256, L"Cannot access Windows audio sessions (0x%08lX).", (unsigned long)hr);
        if (diagnostic) fwprintf(stderr, L"%ls\n", text);
        else MessageBoxW(NULL, text, L"AudioTrace", MB_OK | MB_ICONERROR);
        CoUninitialize(); CloseHandle(mutex); return 1;
    }
    if (diagnostic) {
        refresh_sources(); sample_sessions();
        unsigned endpoints = 0, sessions = 0;
        for (Endpoint *e = app.endpoints; e; e = e->next) ++endpoints;
        for (Session *s = app.sessions; s; s = s->next) {
            ++sessions;
            printf("%s: %s\n", s->metadata.source, s->metadata.detail);
        }
        printf("Playback endpoints: %u; audio sessions: %u; status: 0x%08lX\n",
               endpoints, sessions, (unsigned long)app.lastError);
        BOOL failed = FAILED(app.lastError);
        AcquireSRWLockExclusive(&app.pendingLock); app.closing = TRUE; ReleaseSRWLockExclusive(&app.pendingLock);
        clear_sources(); drain_pending_sessions(TRUE); IMMDeviceEnumerator_Release(app.enumerator);
        CoUninitialize(); CloseHandle(mutex); return failed ? 1 : 0;
    }
    WNDCLASSW cls = {0}; cls.lpfnWndProc = window_proc; cls.hInstance = instance;
    cls.lpszClassName = L"AudioTraceWindow";
    RegisterClassW(&cls);
    /* Hidden top-level window receives the Explorer restart broadcast. */
    app.window = CreateWindowExW(0, cls.lpszClassName, L"AudioTrace", WS_OVERLAPPED,
                                 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!app.window) {
        IMMDeviceEnumerator_Release(app.enumerator); CoUninitialize(); CloseHandle(mutex); return 1;
    }
    app.deviceNotice.window = app.window;
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    app.icon = create_icon();
    if (!app.icon) app.icon = CopyIcon(LoadIconW(NULL, IDI_APPLICATION));
    add_tray();
    if (!app.trayAdded) {
        MessageBoxW(NULL, L"Could not add the AudioTrace tray icon.", L"AudioTrace", MB_OK | MB_ICONERROR);
        DestroyWindow(app.window);
    } else {
        hr = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(app.enumerator, &app.deviceNotice.iface);
        app.deviceNoticeRegistered = SUCCEEDED(hr);
        refresh_sources(); sample_sessions();
        SetTimer(app.window, 1, 20, NULL);
        SetTimer(app.window, 2, 1000, NULL);
    }
    MSG message;
    int result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (result == -1 && IsWindow(app.window)) DestroyWindow(app.window);
    if (app.icon) DestroyIcon(app.icon);
    IMMDeviceEnumerator_Release(app.enumerator);
    CoUninitialize(); CloseHandle(mutex);
    return result == -1 ? 1 : 0;
}
