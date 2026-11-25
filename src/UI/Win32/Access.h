/*
  ZynAddSubFX - a software synthesizer

  Access.h - Screen reader support for standard Windows controls

  Names, descriptions and values of controls are set through Dynamic
  Annotation (IAccPropServices), so NVDA, JAWS and Narrator read what a
  control stands for. Short messages are spoken with announce().

  The approach (a value server for trackbars; announcements both as a UI
  Automation notification and by borrowing the focused control's name,
  because NVDA ignores notifications from classic Win32 windows) follows
  the one proven in BlueWave and Screambox.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#pragma once

#include <windows.h>
#include <functional>
#include <string>

namespace zynui {
namespace access {

//! Once, on the UI thread, after COM is initialised
void init(HWND mainWindow);
void shutdown();

//! The accessible name and description of a control
void setName(HWND h, const std::wstring &name);
void setDescription(HWND h, const std::wstring &text);
//! Forget a control that is about to be destroyed
void forget(HWND h);

//! The accessible value of a control is computed by @p text when asked
//! (trackbars: what the position stands for, in units)
void setValueText(HWND h, std::function<std::wstring(HWND)> text);
//! Tell screen readers that a control's value changed
void valueChanged(HWND h);

//! Speak a short message
void announce(const std::wstring &text);

//! Handles the timer announce() sets; true if it was that timer
bool onTimer(WPARAM id);

}
}
