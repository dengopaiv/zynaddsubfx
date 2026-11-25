/*
  ZynAddSubFX - a software synthesizer

  WinMidiEngine.h - Windows multimedia (winmm) MIDI input

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#ifndef WIN_MIDI_ENGINE_H
#define WIN_MIDI_ENGINE_H

#include <string>
#include <vector>
#include "MidiIn.h"

namespace zyn {

struct SYNTH_T;

/**MIDI input through the Windows multimedia API.
 *
 * Opens the input named by Nio::midiDevice, or every MIDI input on the
 * system when that is empty, and passes all channel messages (notes,
 * controllers, program change, aftertouch, channel pressure, pitch bend)
 * on to MidiIn::midiProcess. SysEx is not read: winmm would need its
 * buffers re-queued from inside the driver callback.*/
class WinMidiEngine:public MidiIn
{
    public:
        WinMidiEngine(const SYNTH_T &synth);
        ~WinMidiEngine();

        bool Start();
        void Stop();

        void setMidiEn(bool nval);
        bool getMidiEn() const;

        //! names of the MIDI inputs on this system, in device order
        static std::vector<std::string> deviceNames();

        //! called from the winmm callback thread
        void handleShort(unsigned long msg);

    private:
        struct Port;
        std::vector<Port *> ports;
        bool running;
};

}

#endif
