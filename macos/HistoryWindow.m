#import "HistoryWindow.h"
#include <math.h>
#include <string.h>

@implementation ATHistoryWindow {
    NSTableView *_table;
    NSScrollView *_scroll;
    NSDateFormatter *_formatter;
    ATHistory _displayed;
}

- (instancetype)init {
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 440)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;
    window.releasedWhenClosed = NO;
    window.title = @"Audio Event History";
    window.minSize = NSMakeSize(500, 240);
    [window center];
    _formatter = [[NSDateFormatter alloc] init];
    _formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    _scroll = [[NSScrollView alloc] initWithFrame:window.contentView.bounds];
    _scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _scroll.hasVerticalScroller = YES;
    _scroll.hasHorizontalScroller = YES;
    _table = [[NSTableView alloc] initWithFrame:_scroll.bounds];
    _table.usesAlternatingRowBackgroundColors = YES;
    _table.allowsMultipleSelection = NO;
    _table.allowsColumnReordering = YES;
    _table.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
    NSArray<NSString *> *identifiers = @[@"time", @"app", @"pid", @"peak", @"details"];
    NSArray<NSString *> *titles = @[@"Time", @"App", @"PID", @"Peak (dBFS)", @"Details"];
    const CGFloat widths[] = {160, 180, 70, 95, 520};
    for (NSUInteger i = 0; i < identifiers.count; ++i) {
        NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:identifiers[i]];
        column.title = titles[i];
        column.width = widths[i];
        column.editable = NO;
        [_table addTableColumn:column];
    }
    _table.dataSource = self;
    _table.delegate = self;
    _scroll.documentView = _table;
    [window.contentView addSubview:_scroll];
    return self;
}

- (void)showWindow:(id)sender {
    [super showWindow:sender];
    [self.window makeFirstResponder:_table];
}

- (NSInteger)findEvent:(const ATEvent *)event {
    for (size_t i = 0; i < _displayed.count; ++i)
        if (memcmp(event, &_displayed.events[i], sizeof(*event)) == 0) return (NSInteger)i;
    return -1;
}

- (void)updateHistory:(const ATHistory *)history {
    NSInteger selected = _table.selectedRow;
    NSPoint origin = _scroll.contentView.bounds.origin;
    NSInteger top = [_table rowAtPoint:origin];
    BOOL keepSelection = selected >= 0 && (size_t)selected < _displayed.count;
    BOOL keepTop = top > 0 && (size_t)top < _displayed.count;
    ATEvent selection = {0}, topEvent = {0};
    CGFloat offset = 0;
    if (keepSelection) selection = _displayed.events[selected];
    if (keepTop) {
        topEvent = _displayed.events[top];
        offset = origin.y - [_table rectOfRow:top].origin.y;
    }
    _displayed = *history;
    [_table reloadData];
    [_table deselectAll:nil];
    if (keepSelection) {
        selected = [self findEvent:&selection];
        if (selected >= 0) [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)selected]
                             byExtendingSelection:NO];
    }
    if (keepTop) {
        top = [self findEvent:&topEvent];
        if (top >= 0) origin.y = [_table rectOfRow:top].origin.y + offset;
    }
    [_scroll.contentView scrollToPoint:origin];
    [_scroll reflectScrolledClipView:_scroll.contentView];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return (NSInteger)_displayed.count;
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    (void)tableView;
    if (row < 0 || (size_t)row >= _displayed.count) return @"";
    const ATEvent *event = &_displayed.events[row];
    if ([column.identifier isEqualToString:@"time"])
        return [_formatter stringFromDate:[NSDate dateWithTimeIntervalSince1970:event->timestamp]];
    if ([column.identifier isEqualToString:@"app"])
        return [NSString stringWithUTF8String:event->source] ?: @"Unknown app";
    if ([column.identifier isEqualToString:@"pid"])
        return [NSString stringWithFormat:@"%u", event->pid];
    if ([column.identifier isEqualToString:@"peak"])
        return [NSString stringWithFormat:@"%.1f", 20 * log10f(fmaxf(event->peak, 1e-9f))];
    return [NSString stringWithUTF8String:event->detail] ?: @"";
}

- (NSString *)tableView:(NSTableView *)tableViewView toolTipForCell:(NSCell *)cell
                  rect:(NSRectPointer)rect tableColumn:(NSTableColumn *)column
                   row:(NSInteger)row mouseLocation:(NSPoint)mouseLocation {
    (void)cell; (void)rect; (void)mouseLocation;
    return [self tableView:tableViewView objectValueForTableColumn:column row:row];
}
@end
