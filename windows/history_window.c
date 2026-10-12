#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "history_window.h"
#include <commctrl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static HWND historyWindow, historyList;
static const ATHistory *source;
static ATHistory displayed;
static BOOL dirty;

void at_history_window_changed(void) { dirty = TRUE; }

static int find_event(const ATEvent *event) {
    for (size_t i = 0; i < displayed.count; ++i)
        if (memcmp(event, &displayed.events[i], sizeof(*event)) == 0) return (int)i;
    return -1;
}

static void refresh_history(void) {
    if (!dirty || !historyList || !source) return;
    int selected = ListView_GetNextItem(historyList, -1, LVNI_SELECTED);
    int top = ListView_GetTopIndex(historyList);
    BOOL keepSelection = selected >= 0 && (size_t)selected < displayed.count;
    BOOL keepTop = top > 0 && (size_t)top < displayed.count;
    ATEvent selection = {0}, topEvent = {0};
    if (keepSelection) selection = displayed.events[selected];
    if (keepTop) topEvent = displayed.events[top];
    displayed = *source;
    dirty = FALSE;
    SendMessageW(historyList, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemState(historyList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(historyList, (int)displayed.count, LVSICF_NOSCROLL);
    if (keepSelection) {
        selected = find_event(&selection);
        if (selected >= 0) ListView_SetItemState(historyList, selected,
            LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    if (keepTop) {
        int newTop = find_event(&topEvent);
        RECT bounds;
        if (newTop >= 0 && ListView_GetItemRect(historyList, newTop, &bounds, LVIR_BOUNDS))
            ListView_Scroll(historyList, 0, (newTop - ListView_GetTopIndex(historyList)) * (bounds.bottom - bounds.top));
    }
    SendMessageW(historyList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(historyList, NULL, TRUE);
}

static void cell_text(const ATEvent *event, int column, wchar_t *text, int capacity) {
    if (capacity <= 0) return;
    text[0] = 0;
    switch (column) {
        case 0: {
            ULARGE_INTEGER ticks;
            ticks.QuadPart = (ULONGLONG)((event->timestamp + 11644473600.0) * 1e7);
            FILETIME utc = {ticks.LowPart, ticks.HighPart}, local;
            SYSTEMTIME time = {0};
            if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &time))
                swprintf(text, capacity, L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear,
                    time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
            break;
        }
        case 1: case 4: {
            wchar_t value[512];
            if (MultiByteToWideChar(CP_UTF8, 0, column == 1 ? event->source : event->detail,
                                   -1, value, 512)) lstrcpynW(text, value, capacity);
            break;
        }
        case 2: swprintf(text, capacity, L"%lu", (unsigned long)event->pid); break;
        case 3: swprintf(text, capacity, L"%.1f", 20 * log10(fmax(event->peak, 1e-9f))); break;
    }
    text[capacity - 1] = 0;
}

static LRESULT CALLBACK list_proc(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                 UINT_PTR subclassID, DWORD_PTR data) {
    (void)subclassID; (void)data;
    if (message == WM_KEYDOWN && wp == VK_ESCAPE) {
        PostMessageW(GetParent(window), WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, list_proc, 1);
    return DefSubclassProc(window, message, wp, lp);
}

static LRESULT CALLBACK history_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_CREATE: {
            historyWindow = window;
            historyList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Traces",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, window, NULL, ((LPCREATESTRUCTW)lp)->hInstance, NULL);
            if (!historyList) return -1;
            ListView_SetExtendedListViewStyle(historyList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            SendMessageW(historyList, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            const wchar_t *titles[] = {L"Time", L"App", L"PID", L"Peak (dBFS)", L"Details"};
            const int widths[] = {155, 180, 70, 95, 520};
            for (int i = 0; i < 5; ++i) {
                LVCOLUMNW column = {0};
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.pszText = (LPWSTR)titles[i]; column.cx = widths[i]; column.iSubItem = i;
                ListView_InsertColumn(historyList, i, &column);
            }
            SetWindowSubclass(historyList, list_proc, 1, 0);
            SetTimer(window, 1, 200, NULL);
            return 0;
        }
        case WM_SIZE:
            MoveWindow(historyList, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_SETFOCUS: SetFocus(historyList); return 0;
        case WM_TIMER: refresh_history(); return 0;
        case WM_NOTIFY: {
            NMHDR *header = (NMHDR *)lp;
            if (header->hwndFrom == historyList && header->code == LVN_GETDISPINFOW) {
                NMLVDISPINFOW *info = (NMLVDISPINFOW *)lp;
                if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0 &&
                    (size_t)info->item.iItem < displayed.count)
                    cell_text(&displayed.events[info->item.iItem], info->item.iSubItem,
                              info->item.pszText, info->item.cchTextMax);
            }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, 1);
            historyWindow = historyList = NULL;
            source = NULL;
            at_history_clear(&displayed);
            return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

void at_history_window_show(HINSTANCE instance, HWND owner, const ATHistory *history) {
    source = history;
    if (!historyWindow) {
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        WNDCLASSW cls = {0};
        cls.lpfnWndProc = history_proc; cls.hInstance = instance;
        cls.lpszClassName = L"AudioTraceHistory"; cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
        cls.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        RegisterClassW(&cls);
        historyWindow = CreateWindowExW(0, cls.lpszClassName, L"Audio Event History",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 480,
            owner, NULL, instance, NULL);
        if (!historyWindow) {
            MessageBoxW(owner, L"Could not open the trace history window.", L"AudioTrace", MB_OK | MB_ICONERROR);
            return;
        }
    }
    dirty = TRUE;
    refresh_history();
    ShowWindow(historyWindow, IsIconic(historyWindow) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(historyWindow);
    SetFocus(historyList);
}
