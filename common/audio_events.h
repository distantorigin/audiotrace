#ifndef AUDIO_TRACE_EVENTS_H
#define AUDIO_TRACE_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define AT_HISTORY_CAPACITY 500
#define AT_MENU_CAPACITY 10
#define AT_QUEUE_CAPACITY 16
#define AT_PEAK_THRESHOLD 0.001f
#define AT_SILENCE_SECONDS 0.5

typedef struct {
    bool active;
    double last_above;
} ATDetector;

typedef struct {
    double timestamp; /* Unix seconds, for display only. */
    double order; /* Monotonic onset time, for history ordering. */
    uint32_t pid;
    float peak;
    char source[256]; /* UTF-8, copied so exited apps remain identifiable. */
    char detail[512];
} ATEvent;

typedef struct {
    ATEvent events[AT_HISTORY_CAPACITY]; /* Newest onset first. */
    size_t count;
} ATHistory;

typedef struct {
    double time; /* Monotonic seconds. */
    double timestamp; /* Unix seconds captured at onset. */
    float peak;
} ATOnset;

/* Single producer (audio callback), single consumer (UI thread). */
typedef struct {
    ATOnset items[AT_QUEUE_CAPACITY];
    atomic_uint write_index;
    atomic_uint read_index;
} ATOnsetQueue;

bool at_detector_observe(ATDetector *detector, float peak, double monotonic_time);
void at_history_push(ATHistory *history, const ATEvent *event);
void at_history_clear(ATHistory *history);
void at_queue_init(ATOnsetQueue *queue);
bool at_queue_push(ATOnsetQueue *queue, ATOnset onset);
bool at_queue_pop(ATOnsetQueue *queue, ATOnset *onset);

#endif
