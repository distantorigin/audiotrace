#import "../macos/HistoryWindow.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        ATHistoryWindow *controller = [[ATHistoryWindow alloc] init];
        NSScrollView *scroll = (NSScrollView *)controller.window.contentView.subviews.firstObject;
        NSTableView *table = (NSTableView *)scroll.documentView;
        static ATHistory history;
        [controller updateHistory:&history];
        assert(table.numberOfRows == 0 && table.numberOfColumns == 5);
        for (unsigned i = 1; i <= 600; ++i) {
            ATEvent event = {.order = i, .timestamp = 1700000000 + i, .pid = i, .peak = 0.1f};
            snprintf(event.source, sizeof(event.source), "App %u", i);
            snprintf(event.detail, sizeof(event.detail), "Details %u", i);
            at_history_push(&history, &event);
        }
        [controller updateHistory:&history];
        assert(table.numberOfRows == 500);
        assert([[[table preparedCellAtColumn:1 row:0] stringValue] isEqualToString:@"App 600"]);
        assert([[[table preparedCellAtColumn:1 row:499] stringValue] isEqualToString:@"App 101"]);
        assert([[[table preparedCellAtColumn:2 row:0] stringValue] isEqualToString:@"600"]);
        assert([[[table preparedCellAtColumn:3 row:0] stringValue] isEqualToString:@"-20.0"]);
        assert([[[table preparedCellAtColumn:4 row:0] stringValue] isEqualToString:@"Details 600"]);
        [table selectRowIndexes:[NSIndexSet indexSetWithIndex:20] byExtendingSelection:NO];
        ATEvent next = {.order = 601, .pid = 601};
        at_history_push(&history, &next);
        [controller updateHistory:&history];
        assert(table.numberOfRows == 500 && table.selectedRow == 21);
        [table selectRowIndexes:[NSIndexSet indexSetWithIndex:499] byExtendingSelection:NO];
        next.order = 602; next.pid = 602;
        at_history_push(&history, &next);
        [controller updateHistory:&history];
        assert(table.selectedRow == -1);
        at_history_clear(&history);
        [controller updateHistory:&history];
        assert(table.numberOfRows == 0);
        NSWindow *window = controller.window;
        [window close];
        [controller updateHistory:&history];
        assert(controller.window == window && table.numberOfRows == 0);
        puts("Native history table: rows, cells, updates, selection, clear, and closed-window reuse passed.");
    }
    return 0;
}
