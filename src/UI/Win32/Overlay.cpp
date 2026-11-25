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
#include <cmath>
#include <cwctype>
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
    //names the descriptions do not give well
    static const std::map<std::string, std::wstring> names = {
        {"efftype",  L"Effect type"},
        {"preset",   L"Effect preset"},
        {"Pkeyshift", L"Key shift"},
    };
    auto it = names.find(base);
    if(it != names.end())
        return it->second;

    std::string s = firstLine(doc);
    //"Panning of ADsynth (0 random, 1 left, 127 right)": the explanation
    //stays in the description
    size_t paren = s.find(" (");
    if(paren != std::string::npos && paren > 3 && s.back() == ')')
        s = s.substr(0, paren);
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

static bool contains(const std::wstring &hay, const wchar_t *needle)
{
    std::wstring h = hay, n = needle;
    for(auto &c : h) c = (wchar_t)towlower(c);
    for(auto &c : n) c = (wchar_t)towlower(c);
    return h.find(n) != std::wstring::npos;
}

static std::wstring percent(double v)
{
    return std::to_wstring((long long)std::lround(v)) + L" %";
}

//! MIDI note number as a name: 60 is C4
static std::wstring noteName(int n)
{
    static const wchar_t *names[12] = {L"C", L"C sharp", L"D", L"D sharp",
        L"E", L"F", L"F sharp", L"G", L"G sharp", L"A", L"A sharp", L"B"};
    if(n < 0 || n > 127)
        return std::to_wstring(n);
    return std::wstring(names[n % 12]) + std::to_wstring(n / 12 - 1) +
           L" (" + std::to_wstring(n) + L")";
}

std::wstring formatValue(const Param &p, double v)
{
    if(!p.options.empty() || p.type == 'T' || p.type == 's')
        return std::wstring();
    const int iv = (int)std::lround(v);
    const bool int127 = p.type != 'f' && p.min == 0 && p.max == 127;

    //Panning: 64 is the centre; some panners use 0 for "random"
    if(int127 && contains(p.label + L" " + widen(p.name), L"pan")) {
        if(iv == 0 && contains(p.doc, L"random"))
            return L"random";
        if(iv == 64)
            return L"centre";
        if(iv < 64)
            return L"left " + percent((64 - v) * 100.0 / 64.0);
        return L"right " + percent((v - 64) * 100.0 / 63.0);
    }
    //Fine detune: 8192 is no detune
    if(p.type != 'f' && p.min == 0 && p.max == 16383 &&
       contains(widen(p.name), L"detune")) {
        const int d = iv - 8192;
        return d == 0 ? L"0, none" : (d > 0 ? L"+" : L"") + std::to_wstring(d);
    }
    //Key ranges
    if(int127 && (p.name == "Pminkey" || p.name == "Pmaxkey"))
        return noteName(iv);
    //Key shift stored as 64 + semitones
    if(int127 && p.name == "Pkeyshift") {
        const int d = iv - 64;
        return (d > 0 ? L"+" : L"") + std::to_wstring(d) + L" semitones";
    }
    return std::wstring();
}

}
}
