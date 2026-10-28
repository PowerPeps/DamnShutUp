#pragma once

#include <windows.h>

// Layered overlay windows drawn left of the minimize button of every window using the standard caption.
namespace buttons {

void Init(HINSTANCE instance);
void Shutdown();
// Full pass over top-level windows, catches anything the event hooks missed.
void Resync();
void ScheduleRecolor();

}  // namespace buttons
