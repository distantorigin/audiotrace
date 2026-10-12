#import <AppKit/AppKit.h>
#include "../common/audio_events.h"

@interface ATHistoryWindow : NSWindowController <NSTableViewDataSource, NSTableViewDelegate>
- (void)updateHistory:(const ATHistory *)history;
@end
