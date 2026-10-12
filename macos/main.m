#import <AppKit/AppKit.h>
#import <CoreAudio/CoreAudio.h>
#import "ProcessTap.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

@interface ATAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate, NSMenuItemValidation>
@end

@implementation ATAppDelegate {
    NSStatusItem *_statusItem;
    NSMenu *_menu;
    NSMutableDictionary<NSNumber *, ATProcessTap *> *_taps;
    NSTimer *_drainTimer;
    NSTimer *_refreshTimer;
    AudioObjectPropertyListenerBlock _processListener;
    ATHistory _history;
    NSDateFormatter *_formatter;
    NSString *_lastError;
    BOOL _paused;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    _taps = [NSMutableDictionary dictionary];
    _formatter = [[NSDateFormatter alloc] init];
    _formatter.dateFormat = @"HH:mm:ss";

    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    _statusItem.button.image = [NSImage imageWithSystemSymbolName:@"waveform" accessibilityDescription:@"AudioTrace"];
    _statusItem.button.toolTip = @"AudioTrace";
    _menu = [[NSMenu alloc] initWithTitle:@"AudioTrace"];
    _menu.delegate = self;
    _statusItem.menu = _menu;

    __weak ATAppDelegate *weakSelf = self;
    _processListener = ^(UInt32 count, const AudioObjectPropertyAddress *addresses) {
        (void)count; (void)addresses;
        [weakSelf refreshProcesses];
    };
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyProcessObjectList,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &address,
        dispatch_get_main_queue(), _processListener);
    /* The timer is also a fallback for listener/setup races and transient errors. */
    _refreshTimer = [NSTimer timerWithTimeInterval:2.0 repeats:YES block:^(NSTimer *timer) {
        (void)timer; [weakSelf refreshProcesses];
    }];
    _drainTimer = [NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) {
        (void)timer; [weakSelf drainEvents];
    }];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
    [[NSRunLoop mainRunLoop] addTimer:_drainTimer forMode:NSRunLoopCommonModes];
    [self refreshProcesses];
}

- (void)refreshProcesses {
    if (_paused) return;
    [self drainEvents];
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyProcessObjectList,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, NULL, &size);
    if (status != noErr) {
        _lastError = [NSString stringWithFormat:@"Cannot list audio processes (OSStatus %d)", (int)status];
        return;
    }
    NSMutableData *data = [NSMutableData dataWithLength:size];
    if (size) {
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, data.mutableBytes);
        if (status != noErr) return; /* Process list changed; retry on next notification/timer. */
    }
    NSMutableSet<NSNumber *> *present = [NSMutableSet set];
    _lastError = nil;
    AudioObjectID *processes = data.mutableBytes;
    for (UInt32 i = 0; i < size / sizeof(AudioObjectID); ++i) {
        AudioObjectID processID = processes[i];
        pid_t pid = 0;
        UInt32 pidSize = sizeof(pid);
        AudioObjectPropertyAddress pidAddress = {kAudioProcessPropertyPID,
            kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyData(processID, &pidAddress, 0, NULL, &pidSize, &pid) != noErr ||
            pid <= 0 || pid == getpid()) continue;
        NSNumber *key = @(processID);
        [present addObject:key];
        if (_taps[key].pid == pid) continue;
        [_taps[key] stop];
        [self drainTap:_taps[key]];
        [_taps removeObjectForKey:key];
        ATProcessTap *tap = [[ATProcessTap alloc] initWithProcessID:processID pid:pid];
        NSError *error = nil;
        if ([tap startWithError:&error]) _taps[key] = tap;
        else _lastError = error.localizedDescription;
    }
    for (NSNumber *key in _taps.allKeys) {
        if (![present containsObject:key]) {
            [_taps[key] stop];
            [self drainTap:_taps[key]];
            [_taps removeObjectForKey:key];
        }
    }
}

- (void)drainEvents {
    if (_paused) return;
    for (ATProcessTap *tap in _taps.allValues) [self drainTap:tap];
}

- (void)drainTap:(ATProcessTap *)tap {
    ATOnset onset;
    while ([tap popOnset:&onset]) {
        ATEvent event = {.timestamp = onset.timestamp, .order = onset.time,
            .pid = (uint32_t)tap.pid, .peak = onset.peak};
        snprintf(event.source, sizeof(event.source), "%s", tap.source.UTF8String);
        snprintf(event.detail, sizeof(event.detail), "%s", tap.detail.UTF8String);
        at_history_push(&_history, &event);
    }
}

- (NSMenuItem *)item:(NSString *)title action:(SEL)action {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    item.target = self;
    return item;
}

- (NSString *)lineForEvent:(const ATEvent *)event {
    NSString *time = [_formatter stringFromDate:[NSDate dateWithTimeIntervalSince1970:event->timestamp]];
    NSString *source = [[NSString alloc] initWithBytes:event->source length:strlen(event->source)
        encoding:NSUTF8StringEncoding] ?: @"Unknown app";
    return [NSString stringWithFormat:@"%@  %@", time, source];
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    [self drainEvents];
    [menu removeAllItems];
    NSString *status = _paused ? @"Paused" : @"Monitoring";
    [menu addItem:[self item:status action:NULL]];
    [menu addItem:[NSMenuItem separatorItem]];
    if (!_history.count) [menu addItem:[self item:@"No sounds detected yet" action:NULL]];
    for (size_t i = 0; i < _history.count; ++i) {
        const ATEvent *event = &_history.events[i];
        NSMenuItem *item = [self item:[self lineForEvent:event] action:NULL];
        NSString *detail = [NSString stringWithUTF8String:event->detail] ?: @"";
        item.toolTip = [NSString stringWithFormat:@"%@\nPeak at onset: %.1f dBFS", detail,
                       20 * log10f(fmaxf(event->peak, 1e-9f))];
        /* A submenu keeps provenance available even when the process has exited. */
        NSMenu *details = [[NSMenu alloc] initWithTitle:detail];
        [details addItem:[self item:detail action:NULL]];
        item.submenu = details;
        [menu addItem:item];
    }
    if (_lastError) {
        [menu addItem:[NSMenuItem separatorItem]];
        NSMenuItem *item = [self item:@"Some audio sources are unavailable" action:NULL];
        NSMenu *details = [[NSMenu alloc] initWithTitle:@"Capture error"];
        [details addItem:[self item:_lastError action:NULL]];
        item.submenu = details;
        [menu addItem:item];
    }
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItem:[self item:_paused ? @"Resume monitoring" : @"Pause monitoring" action:@selector(togglePause:)]];
    NSMenuItem *copy = [self item:@"Copy history" action:@selector(copyHistory:)];
    [menu addItem:copy];
    [menu addItem:[self item:@"Clear history" action:@selector(clearHistory:)]];
    [menu addItem:[self item:@"Audio recording permissions…" action:@selector(openPermissions:)]];
    [menu addItem:[self item:@"Quit AudioTrace" action:@selector(quit:)]];
}

- (void)togglePause:(id)sender {
    (void)sender;
    _paused = !_paused;
    if (_paused) {
        for (ATProcessTap *tap in _taps.allValues) {
            [tap stop];
            [self drainTap:tap];
        }
        [_taps removeAllObjects];
    } else [self refreshProcesses];
    _statusItem.button.image = [NSImage imageWithSystemSymbolName:_paused ? @"pause.circle" : @"waveform"
        accessibilityDescription:@"AudioTrace"];
}

- (void)clearHistory:(id)sender { (void)sender; [self drainEvents]; at_history_clear(&_history); }
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    return item.action != @selector(copyHistory:) || _history.count > 0;
}
- (void)copyHistory:(id)sender {
    (void)sender;
    if (!_history.count) return;
    NSMutableArray<NSString *> *lines = [NSMutableArray array];
    for (size_t i = 0; i < _history.count; ++i) {
        [lines addObject:[NSString stringWithFormat:@"%@\t%s", [self lineForEvent:&_history.events[i]],
                          _history.events[i].detail]];
    }
    [[NSPasteboard generalPasteboard] clearContents];
    [[NSPasteboard generalPasteboard] setString:[lines componentsJoinedByString:@"\n"] forType:NSPasteboardTypeString];
}
- (void)openPermissions:(id)sender {
    (void)sender;
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:
        @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}
- (void)quit:(id)sender { (void)sender; [NSApp terminate:nil]; }

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [_drainTimer invalidate];
    [_refreshTimer invalidate];
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyProcessObjectList,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (_processListener) AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &address,
        dispatch_get_main_queue(), _processListener);
    for (ATProcessTap *tap in _taps.allValues) [tap stop];
    [_taps removeAllObjects];
}
@end

int main(void) {
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
        __attribute__((objc_precise_lifetime)) ATAppDelegate *delegate = [[ATAppDelegate alloc] init];
        application.delegate = delegate;
        [application run];
        (void)delegate;
    }
    return 0;
}
