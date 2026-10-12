#include "audio_events.h"
#include <math.h>
#include <string.h>

bool at_detector_observe(ATDetector *detector, float peak, double now) {
    if (!isfinite(peak) || !isfinite(now) || peak < 0) return false;
    if (peak > AT_PEAK_THRESHOLD) {
        bool onset = !detector->active || now - detector->last_above >= AT_SILENCE_SECONDS;
        detector->active = true;
        detector->last_above = now;
        return onset;
    }
    if (detector->active && now - detector->last_above >= AT_SILENCE_SECONDS) {
        detector->active = false;
    }
    return false;
}

void at_history_push(ATHistory *history, const ATEvent *event) {
    size_t index = 0;
    while (index < history->count && history->events[index].order > event->order) ++index;
    if (index == AT_HISTORY_CAPACITY) return;
    size_t count = history->count < AT_HISTORY_CAPACITY ? history->count + 1 : history->count;
    memmove(&history->events[index + 1], &history->events[index],
            (count - index - 1) * sizeof(ATEvent));
    history->events[index] = *event;
    history->count = count;
}

void at_history_clear(ATHistory *history) {
    memset(history, 0, sizeof(*history));
}

void at_queue_init(ATOnsetQueue *queue) {
    atomic_init(&queue->write_index, 0);
    atomic_init(&queue->read_index, 0);
}

bool at_queue_push(ATOnsetQueue *queue, ATOnset onset) {
    unsigned write = atomic_load_explicit(&queue->write_index, memory_order_relaxed);
    unsigned read = atomic_load_explicit(&queue->read_index, memory_order_acquire);
    if (write - read >= AT_QUEUE_CAPACITY) return false;
    queue->items[write % AT_QUEUE_CAPACITY] = onset;
    atomic_store_explicit(&queue->write_index, write + 1, memory_order_release);
    return true;
}

bool at_queue_pop(ATOnsetQueue *queue, ATOnset *onset) {
    unsigned read = atomic_load_explicit(&queue->read_index, memory_order_relaxed);
    unsigned write = atomic_load_explicit(&queue->write_index, memory_order_acquire);
    if (read == write) return false;
    *onset = queue->items[read % AT_QUEUE_CAPACITY];
    atomic_store_explicit(&queue->read_index, read + 1, memory_order_release);
    return true;
}
