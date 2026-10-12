#import "ProcessTap.h"
#import <AppKit/AppKit.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <libproc.h>
#import <mach/mach_time.h>
#include <math.h>
#include <string.h>
#include <time.h>

typedef struct {
    ATDetector detector;
    ATOnsetQueue queue;
    AudioStreamBasicDescription format;
    double secondsPerTick;
} ATCaptureState;

/* Called on Core Audio's real-time thread: no allocation, locks, or Obj-C. */
static OSStatus CaptureAudio(AudioObjectID device, const AudioTimeStamp *now,
                             const AudioBufferList *input, const AudioTimeStamp *inputTime,
                             AudioBufferList *output, const AudioTimeStamp *outputTime,
                             void *context) {
    (void)device; (void)now; (void)inputTime; (void)outputTime;
    ATCaptureState *state = context;
    float peak = 0;
    if (input) {
        for (UInt32 b = 0; b < input->mNumberBuffers; ++b) {
            const AudioBuffer *buffer = &input->mBuffers[b];
            if (!buffer->mData) continue;
            UInt32 bits = state->format.mBitsPerChannel;
            size_t count = buffer->mDataByteSize / (bits / 8);
            for (size_t s = 0; s < count; ++s) {
                double value;
                if (state->format.mFormatFlags & kAudioFormatFlagIsFloat) {
                    value = bits == 32 ? ((const float *)buffer->mData)[s]
                                       : ((const double *)buffer->mData)[s];
                } else {
                    value = bits == 16 ? ((const int16_t *)buffer->mData)[s] / 32768.0
                                       : ((const int32_t *)buffer->mData)[s] / 2147483648.0;
                }
                value = fabs(value);
                if (isfinite(value) && value > peak) peak = (float)value;
            }
        }
    }
    /* The private aggregate has no hardware output; never replay captured audio. */
    if (output) {
        for (UInt32 b = 0; b < output->mNumberBuffers; ++b) {
            if (output->mBuffers[b].mData)
                memset(output->mBuffers[b].mData, 0, output->mBuffers[b].mDataByteSize);
        }
    }
    double time = mach_continuous_time() * state->secondsPerTick;
    if (at_detector_observe(&state->detector, peak, time)) {
        struct timespec wall;
        clock_gettime(CLOCK_REALTIME, &wall);
        at_queue_push(&state->queue, (ATOnset){.time = time,
            .timestamp = wall.tv_sec + wall.tv_nsec / 1e9, .peak = peak});
    }
    return noErr;
}

@implementation ATProcessTap {
    AudioObjectID _tap;
    AudioObjectID _aggregate;
    AudioDeviceIOProcID _ioProc;
    ATCaptureState _capture;
}

- (instancetype)initWithProcessID:(AudioObjectID)processID pid:(pid_t)pid {
    self = [super init];
    if (!self) return nil;
    _processID = processID;
    _pid = pid;
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    char name[PROC_PIDPATHINFO_MAXSIZE] = {0};
    proc_name(pid, name, sizeof(name));
    NSString *fallback = name[0] ? [NSString stringWithUTF8String:name] : nil;
    _source = app.localizedName ?: fallback ?: [NSString stringWithFormat:@"Process %d", pid];
    CFStringRef bundleID = NULL;
    UInt32 size = sizeof(bundleID);
    AudioObjectPropertyAddress address = {kAudioProcessPropertyBundleID,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectGetPropertyData(processID, &address, 0, NULL, &size, &bundleID);
    NSString *bundle = CFBridgingRelease(bundleID);
    _detail = [NSString stringWithFormat:@"PID %d%@", pid,
               bundle.length ? [@" · " stringByAppendingString:bundle] : @""];
    return self;
}

- (BOOL)startWithError:(NSError **)error {
    if (_tap) return YES;
    memset(&_capture.detector, 0, sizeof(_capture.detector));
    at_queue_init(&_capture.queue);
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    _capture.secondsPerTick = (double)timebase.numer / timebase.denom / 1e9;

    CATapDescription *description = [[CATapDescription alloc] initStereoMixdownOfProcesses:@[@(_processID)]];
    description.name = [@"AudioTrace: " stringByAppendingString:_source];
    description.privateTap = YES;
    description.muteBehavior = CATapUnmuted;
    description.UUID = [NSUUID UUID];
    NSString *operation = @"Create audio tap";
    OSStatus status = AudioHardwareCreateProcessTap(description, &_tap);
    if (status != noErr) goto failed;

    operation = @"Read tap format";
    AudioObjectPropertyAddress address;
    address = (AudioObjectPropertyAddress){kAudioTapPropertyFormat,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size;
    size = sizeof(_capture.format);
    status = AudioObjectGetPropertyData(_tap, &address, 0, NULL, &size, &_capture.format);
    if (status != noErr) goto failed;
    /* Check sample layout before the callback interprets its raw buffers. */
    AudioStreamBasicDescription format;
    format = _capture.format;
    BOOL floating, integer, packed;
    floating = (format.mFormatFlags & kAudioFormatFlagIsFloat) &&
               (format.mBitsPerChannel == 32 || format.mBitsPerChannel == 64);
    integer = (format.mFormatFlags & kAudioFormatFlagIsSignedInteger) &&
              (format.mBitsPerChannel == 16 || format.mBitsPerChannel == 32);
    UInt32 channelsInBuffer;
    channelsInBuffer = (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) ? 1 : format.mChannelsPerFrame;
    packed = format.mBytesPerFrame == channelsInBuffer * (format.mBitsPerChannel / 8);
    if (format.mFormatID != kAudioFormatLinearPCM || !(floating || integer) || !packed ||
        format.mChannelsPerFrame == 0 || (format.mFormatFlags & kAudioFormatFlagIsBigEndian)) {
        operation = @"Unsupported tap sample format";
        status = kAudioHardwareUnsupportedOperationError;
        goto failed;
    }

    /* Tap-only private aggregate: it never changes the default output device. */
    {
        NSDictionary *aggregateDescription = @{
            @kAudioAggregateDeviceNameKey: @"AudioTrace monitor",
            @kAudioAggregateDeviceUIDKey: [NSUUID UUID].UUIDString,
            @kAudioAggregateDeviceIsPrivateKey: @YES,
            @kAudioAggregateDeviceTapAutoStartKey: @YES,
            @kAudioAggregateDeviceTapListKey: @[@{
                @kAudioSubTapUIDKey: description.UUID.UUIDString,
                @kAudioSubTapDriftCompensationKey: @YES
            }]
        };
        operation = @"Create private tap device";
        status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggregateDescription, &_aggregate);
    }
    if (status != noErr) goto failed;
    operation = @"Install audio callback";
    status = AudioDeviceCreateIOProcID(_aggregate, CaptureAudio, &_capture, &_ioProc);
    if (status != noErr) goto failed;
    operation = @"Start audio capture (check System Audio Recording permission)";
    status = AudioDeviceStart(_aggregate, _ioProc);
    if (status != noErr) goto failed;
    return YES;

failed:
    if (error) {
        *error = [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:@{
            NSLocalizedDescriptionKey: [NSString stringWithFormat:@"%@: %@ (OSStatus %d)",
                _source, operation, (int)status]
        }];
    }
    [self stop];
    return NO;
}

- (BOOL)popOnset:(ATOnset *)onset {
    return at_queue_pop(&_capture.queue, onset);
}

- (void)stop {
    /* Stop and unregister the callback before releasing its context. */
    if (_aggregate && _ioProc) {
        AudioDeviceStop(_aggregate, _ioProc);
        AudioDeviceDestroyIOProcID(_aggregate, _ioProc);
        _ioProc = NULL;
    }
    if (_aggregate) {
        AudioHardwareDestroyAggregateDevice(_aggregate);
        _aggregate = 0;
    }
    if (_tap) {
        AudioHardwareDestroyProcessTap(_tap);
        _tap = 0;
    }
}

- (void)dealloc { [self stop]; }
@end
