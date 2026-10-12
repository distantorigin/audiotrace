#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import "../common/audio_events.h"

@interface ATProcessTap : NSObject
@property(nonatomic, readonly) AudioObjectID processID;
@property(nonatomic, readonly) pid_t pid;
@property(nonatomic, readonly, copy) NSString *source;
@property(nonatomic, readonly, copy) NSString *detail;
- (instancetype)initWithProcessID:(AudioObjectID)processID pid:(pid_t)pid;
- (BOOL)startWithError:(NSError **)error;
- (BOOL)popOnset:(ATOnset *)onset;
- (void)stop;
@end
