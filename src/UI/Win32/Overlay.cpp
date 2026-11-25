/*
  ZynAddSubFX - a software synthesizer

  Overlay.cpp - What the port metadata does not say

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#include "Overlay.h"
#include "PortModel.h"

#include <cctype>
#include <cstring>
#include <map>
#include <set>

namespace zynui {
namespace overlay {

bool hiddenGroup(const std::string &base)
{
    static const std::set<std::string> hidden = {
        "bank",       //the bank browser has its own dialog
        "presets",    //copy and paste of presets: Edit menu
        "io",         //audio and MIDI devices: Options dialog
        "automate",   //MIDI learn: its own dialog
        "watch",      //engine internals
        "HDDRecorder",
        "Pmidi",
        "self",
        "parameter",
    };
    return hidden.count(base) != 0;
}

std::wstring groupLabel(const std::string &base, const char *doc)
{
    static const std::map<std::string, std::wstring> names = {
        {"part",            L"Part"},
        {"kit",             L"Kit item"},
        {"adpars",          L"AddSynth"},
        {"subpars",         L"SubSynth"},
        {"padpars",         L"PADsynth"},
        {"GlobalPar",       L"Global"},
        {"VoicePar",        L"Voice"},
        {"AmpEnvelope",     L"Amplitude envelope"},
        {"FreqEnvelope",    L"Frequency envelope"},
        {"FilterEnvelope",  L"Filter envelope"},
        {"BandWidthEnvelope", L"Bandwidth envelope"},
        {"FMAmpEnvelope",   L"Modulator amplitude envelope"},
        {"FMFreqEnvelope",  L"Modulator frequency envelope"},
        {"AmpLfo",          L"Amplitude LFO"},
        {"FreqLfo",         L"Frequency LFO"},
        {"FilterLfo",       L"Filter LFO"},
        {"GlobalFilter",    L"Filter"},
        {"VoiceFilter",     L"Filter"},
        {"filterpars",      L"Filter"},
        {"OscilSmp",        L"Oscillator"},
        {"FMSmp",           L"Modulator oscillator"},
        {"oscilgen",        L"Oscillator"},
        {"Reson",           L"Resonance"},
        {"resonance",       L"Resonance"},
        {"sysefx",          L"System effect"},
        {"insefx",          L"Insertion effect"},
        {"partefx",         L"Part effect"},
        {"microtonal",      L"Tuning"},
        {"ctl",             L"Controllers"},
        {"config",          L"Settings"},
        {"Alienwah",        L"AlienWah"},
        {"DynamicFilter",   L"Dynamic filter"},
        {"filter",          L"Band"},
        {"freqcrossover",   L"Crossover"},
    };
    auto it = names.find(base);
    if(it != names.end())
        return it->second;
    if(doc && *doc && strlen(doc) <= 32 && !strchr(doc, '\n'))
        return widen(doc);
    return widen(base);
}

//! The first line of a description, trimmed, without a final full stop
static std::string firstLine(const std::string &doc)
{
    std::string s = doc.substr(0, doc.find('\n'));
    while(!s.empty() && isspace((unsigned char)s.back()))
        s.pop_back();
    size_t b = 0;
    while(b < s.size() && isspace((unsigned char)s[b]))
        ++b;
    s = s.substr(b);
    if(!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

std::wstring paramLabel(const std::string &, const std::string &base,
                        const std::string &doc, const std::string &shortName)
{
    std::string s = firstLine(doc);
    if(s.empty() || s.size() > 60 || s == base)
        s = !shortName.empty() ? shortName : base;
    if(!s.empty())
        s[0] = (char)toupper((unsigned char)s[0]);
    return widen(s);
}

//! Time parameters the engine keeps in seconds without saying so
static void addUnits(Param &p)
{
    if(!p.unit.empty() || p.type != 'f')
        return;
    const std::string &n = p.name;
    if(n == "A_dt" || n == "D_dt" || n == "R_dt" || n == "delay" ||
       n == "fadein" || n == "fadeout")
        p.unit = "S";
}

void arrange(const std::string &, std::vector<Param> &params)
{
    for(Param &p : params)
        addUnits(p);

    //The engine keeps old integer ports beside their newer float ones
    //(Pvolume and Volume, PA_dt and A_dt ...), with the same description.
    //Show the float one, which has the unit.
    std::map<std::wstring, int> floats, count;
    for(const Param &p : params) {
        ++count[p.label];
        if(p.type == 'f')
            ++floats[p.label];
    }
    std::vector<Param> kept;
    for(Param &p : params) {
        if(count[p.label] > 1 && floats[p.label] > 0 && p.type != 'f')
            continue;
        kept.push_back(p);
    }
    //Still the same name: different parameters with one description
    std::map<std::wstring, int> seen;
    for(const Param &p : kept)
        ++seen[p.label];
    for(Param &p : kept)
        if(seen[p.label] > 1)
            p.label += L" (" + widen(p.name) + L")";
    params.swap(kept);
}

std::wstring formatValue(const Param &, double)
{
    return std::wstring();
}

}
}
