#define UNICODE
#define _UNICODE
#include "../windows/history_window.h"
#include <commctrl.h>
#include <assert.h>
#include <stdio.h>

static void check_cell(HWND list, int row, int column, const wchar_t *expected) {
    wchar_t text[512] = {0};
    LVITEMW item = {0};
    item.mask = LVIF_TEXT; item.iItem = row; item.iSubItem = column;
    item.pszText = text; item.cchTextMax = 512;
    assert(SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item));
    assert(wcscmp(text, expected) == 0);
}

int main(void) {
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND owner = CreateWindowExW(0, L"STATIC", L"Test monitoring owner", WS_OVERLAPPED,
        0, 0, 1, 1, NULL, NULL, instance, NULL);
    assert(owner);
    static ATHistory history;
    at_history_window_show(instance, owner, &history);
    HWND window = FindWindowW(L"AudioTraceHistory", NULL);
    assert(window);
    ShowWindow(window, SW_HIDE);
    HWND list = FindWindowExW(window, NULL, WC_LISTVIEWW, NULL);
    assert(list && ListView_GetItemCount(list) == 0);
    assert(Header_GetItemCount(ListView_GetHeader(list)) == 5);
    for (unsigned i = 1; i <= 600; ++i) {
        ATEvent event = {.order = i, .timestamp = 1700000000 + i, .pid = i, .peak = 0.1f};
        snprintf(event.source, sizeof(event.source), "App %u", i);
        snprintf(event.detail, sizeof(event.detail), "Details %u", i);
        at_history_push(&history, &event);
    }
    at_history_window_changed();
    SendMessageW(window, WM_TIMER, 1, 0);
    assert(ListView_GetItemCount(list) == 500);
    check_cell(list, 0, 1, L"App 600");
    check_cell(list, 499, 1, L"App 101");
    check_cell(list, 0, 2, L"600");
    check_cell(list, 0, 3, L"-20.0");
    check_cell(list, 0, 4, L"Details 600");
    ListView_SetItemState(list, 20, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ATEvent next = {.order = 601, .pid = 601};
    at_history_push(&history, &next);
    at_history_window_changed();
    SendMessageW(window, WM_TIMER, 1, 0);
    assert(ListView_GetItemCount(list) == 500);
    assert(ListView_GetNextItem(list, -1, LVNI_SELECTED) == 21);
    ListView_SetItemState(list, 499, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    next.order = 602; next.pid = 602;
    at_history_push(&history, &next);
    at_history_window_changed();
    SendMessageW(window, WM_TIMER, 1, 0);
    assert(ListView_GetNextItem(list, -1, LVNI_SELECTED) == -1);
    at_history_clear(&history);
    at_history_window_changed();
    SendMessageW(window, WM_TIMER, 1, 0);
    assert(ListView_GetItemCount(list) == 0);
    at_history_window_show(instance, owner, &history);
    assert(FindWindowW(L"AudioTraceHistory", NULL) == window);
    SendMessageW(window, WM_CLOSE, 0, 0);
    assert(!IsWindow(window) && IsWindow(owner));
    at_history_window_show(instance, owner, &history);
    window = FindWindowW(L"AudioTraceHistory", NULL);
    assert(window);
    DestroyWindow(owner);
    assert(!IsWindow(window));
    puts("Native history list: rows, cells, updates, selection, clear, reopen, and owner lifetime passed.");
    return 0;
}
