#ifndef AUDIO_TRACE_HISTORY_WINDOW_H
#define AUDIO_TRACE_HISTORY_WINDOW_H

#include <windows.h>
#include "../common/audio_events.h"

void at_history_window_show(HINSTANCE instance, HWND owner, const ATHistory *history);
void at_history_window_changed(void);

#endif
