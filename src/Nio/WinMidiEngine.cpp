/*
  ZynAddSubFX - a software synthesizer

  WinMidiEngine.cpp - Windows multimedia (winmm) MIDI input

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#include "WinMidiEngine.h"
#include "Nio.h"

#include <windows.h>
#include <mmsystem.h>
#include <iostream>

namespace zyn {

struct WinMidiEngine::Port {
    HMIDIIN     handle = nullptr;
    std::string name;
};

static std::string toUtf8(const wchar_t *w)
{
    if(!w || !*w)
        return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if(n <= 1)
        return std::string();
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

static void CALLBACK winMidiCallback(HMIDIIN, UINT wMsg, DWORD_PTR instance,
                                     DWORD_PTR param1, DWORD_PTR)
{
    if(wMsg == MIM_DATA)
        reinterpret_cast<WinMidiEngine *>(instance)->handleShort(
            (unsigned long)param1);
}

WinMidiEngine::WinMidiEngine(const SYNTH_T &)
    :running(false)
{
    name = "WINMIDI";
}

WinMidiEngine::~WinMidiEngine()
{
    Stop();
}

std::vector<std::string> WinMidiEngine::deviceNames()
{
    std::vector<std::string> names;
    const UINT n = midiInGetNumDevs();
    for(UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps;
        if(midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            names.push_back(toUtf8(caps.szPname));
        else
            names.push_back("MIDI input " + std::to_string(i + 1));
    }
    return names;
}

bool WinMidiEngine::Start()
{
    if(running)
        return true;

    const std::vector<std::string> names = deviceNames();
    const std::string &wanted = Nio::midiDevice;

    for(UINT i = 0; i < names.size(); ++i) {
        if(!wanted.empty() && names[i] != wanted)
            continue;
        HMIDIIN h = nullptr;
        MMRESULT res = midiInOpen(&h, i, (DWORD_PTR)&winMidiCallback,
                                  (DWORD_PTR)this, CALLBACK_FUNCTION);
        if(res != MMSYSERR_NOERROR) {
            std::cerr << "[WARNING] Could not open MIDI input \""
                      << names[i] << "\" (error " << res << ")" << std::endl;
            continue;
        }
        if(midiInStart(h) != MMSYSERR_NOERROR) {
            midiInClose(h);
            continue;
        }
        Port *p   = new Port;
        p->handle = h;
        p->name   = names[i];
        ports.push_back(p);
        std::cerr << "[INFO] MIDI input: " << names[i] << std::endl;
    }

    if(ports.empty() && !wanted.empty())
        std::cerr << "[WARNING] MIDI input \"" << wanted
                  << "\" is not connected" << std::endl;

    //Running without any device is fine: nothing may be plugged in yet,
    //and choosing this engine again (or another device) rescans.
    running = true;
    return true;
}

void WinMidiEngine::Stop()
{
    for(Port *p : ports) {
        midiInStop(p->handle);
        midiInReset(p->handle);
        midiInClose(p->handle);
        delete p;
    }
    ports.clear();
    running = false;
}

void WinMidiEngine::setMidiEn(bool nval)
{
    if(nval)
        Start();
    else
        Stop();
}

bool WinMidiEngine::getMidiEn() const
{
    return running;
}

void WinMidiEngine::handleShort(unsigned long msg)
{
    const unsigned char status = msg & 0xff;
    if(status < 0x80 || status >= 0xF0) //system messages: clock, sensing...
        return;
    midiProcess(status, (msg >> 8) & 0x7f, (msg >> 16) & 0x7f);
}

}
