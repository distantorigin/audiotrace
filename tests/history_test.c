#include "../common/audio_events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static ATHistory history;
    for (unsigned i = 1; i <= 600; ++i) {
        ATEvent event = {.order = i, .timestamp = i, .pid = i};
        snprintf(event.source, sizeof(event.source), "App %u", i);
        at_history_push(&history, &event);
    }
    assert(history.count == 500);
    for (size_t i = 0; i < 500; ++i) assert(history.events[i].pid == 600 - i);
    /* A delayed event must sort by onset, not arrival, and evict the oldest. */
    ATEvent delayed = {.order = 550.5, .pid = 999};
    at_history_push(&history, &delayed);
    assert(history.count == 500);
    assert(history.events[49].pid == 551);
    assert(history.events[50].pid == 999);
    assert(history.events[51].pid == 550);
    assert(history.events[499].pid == 102);
    ATEvent old = {.order = 1, .pid = 1};
    at_history_push(&history, &old);
    assert(history.count == 500 && history.events[499].pid == 102);
    assert(strcmp(history.events[0].source, "App 600") == 0);
    at_history_clear(&history);
    assert(history.count == 0);
    at_history_push(&history, &old);
    assert(history.count == 1 && history.events[0].pid == 1);
    puts("History retention, ordering, overflow, and clear checks passed.");
    return 0;
}
