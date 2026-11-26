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
        {"Pvowels",         L"Vowel"},
        {"Pformants",       L"Formant"},
        {"Psysefxvol",      L"Sends to system effect"},
        {"sysefxfrom",      L"Routing from system effect"},
        {"Psysefxsend",     L"Sends from system effect"},
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

std::wstring paramLabel(const std::string &nodePath, const std::string &base,
                        const std::string &doc, const std::string &shortName)
{
    //the engine describes a voice's volume as "Part Volume"
    if(base == "volume" && nodePath.find("/VoicePar") != std::string::npos)
        return L"Voice volume";
    //names the descriptions do not give well
    static const std::map<std::string, std::wstring> names = {
        {"efftype",  L"Effect type"},
        {"preset",   L"Effect preset"},
        {"Pkeyshift", L"Key shift"},
        {"magnitude", L"Harmonic magnitude"},
        {"phase",     L"Harmonic phase"},
        {"Penvval",   L"Point value"},
        {"Penvdt",    L"Point time"},
        {"envdt",     L"Point time"},
        {"Prespoints", L"Resonance point"},
        {"Pmapping",  L"Mapped scale degree"},
        {"Pinsparts", L"Where the insertion effect works"},
        {"VoicePar",  L"Voice enabled"},
        {"Hrandgrouping", L"Same randomness for all voices"},
        {"Prand",     L"Phase randomness"},
        {"Pfilterbeforews", L"Filter before waveshaping"},
        {"PVolumeminus", L"Invert the voice (signal inverter)"},
        {"Pscaleshift", L"Scale shift"},
        {"cfg.OscilSize", L"Size Of Oscillator Wavetable"},
        {"cfg.OscilPower", L"Size Of Oscillator Wavetable, as a power of two"},
        {"portamento.proportional", L"Proportional portamento time"},
        {"PAfreq",    L"Frequency of the 'A' note"},
        {"Pefxroute", L"Part effect output"},
        {"Pefxbypass", L"Part effect bypassed"},
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
    if(n == "envdt")
        p.unit = "ms";
    if(n == "PAfreq")
        p.unit = "Hz";
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
        //EffectMgr's "parameter#128": numbered aliases of the effect's own
        //named settings, which the effect type's group shows
        if(p.name == "parameter" && p.count > 0)
            continue;
        //the envelope's point times twice: Penvdt (0 to 127) and envdt
        //(milliseconds); the one in milliseconds is kept
        if(p.name == "Penvdt")
            continue;
        kept.push_back(p);
    }
    //Still the same name: different parameters with one description
    std::map<std::wstring, int> seen;
    for(const Param &p : kept)
        ++seen[p.label];
    for(Param &p : kept)
        if(seen[p.label] > 1) {
            //"... parameter 1", "... parameter 2" when the ports are
            //numbered (Pfilterpar1, Phrpos.par2), else the port's name
            size_t d = p.name.size();
            while(d > 0 && isdigit((unsigned char)p.name[d - 1]))
                --d;
            p.label += d < p.name.size()
                           ? L" " + widen(p.name.substr(d))
                           : L" (" + widen(p.name) + L")";
        }
    params.swap(kept);
}

std::wstring arrayIndexName(const std::string &base)
{
    if(base == "magnitude" || base == "phase")
        return L"Harmonic";
    if(base == "Penvval" || base == "Penvdt" || base == "envval" ||
       base == "envdt" || base == "Prespoints")
        return L"Point";
    if(base == "Pmapping")
        return L"Key";
    if(base == "Pinsparts")
        return L"Insertion effect";
    if(base == "Pefxroute" || base == "Pefxbypass")
        return L"Part effect";
    if(base == "Phmag" || base == "Phrelbw")
        return L"Harmonic";
    if(base == "vowel_seq")
        return L"Step";
    if(base == "part")
        return L"Part";
    if(base == "VoicePar")
        return L"Voice";
    if(base == "to")
        return L"System effect";
    return L"Item";
}

//! The last segment of a node path: "/part0/kit0/adpars/" -> "adpars"
static std::string lastSegment(const std::string &path)
{
    std::string p = path;
    if(!p.empty() && p.back() == '/')
        p.pop_back();
    size_t s = p.rfind('/');
    std::string seg = s == std::string::npos ? p : p.substr(s + 1);
    while(!seg.empty() && isdigit((unsigned char)seg.back()))
        seg.pop_back(); //"part0" -> "part"
    return seg;
}

static Param action(const std::string &nodePath, const char *port,
                    const wchar_t *label, const wchar_t *doc, int arg = -1)
{
    Param a;
    a.name      = port;
    a.path      = nodePath + port;
    a.label     = label;
    a.doc       = doc;
    a.action    = true;
    a.actionArg = arg;
    return a;
}

void addExtras(const Node &n, std::vector<Param> &params)
{
    const std::string seg = lastSegment(n.path);
    if(n.path == "/")
        params.push_back(action("/", "Panic", L"Stop all sound",
                                L"Silences every note at once"));
    if(seg == "part" && n.path.find('/', 1) == n.path.size() - 1) {
        params.push_back(action(n.path, "captureMin",
            L"Lowest key: take the last note played",
            L"Sets the lowest key of the part to the last note played"));
        params.push_back(action(n.path, "captureMax",
            L"Highest key: take the last note played",
            L"Sets the highest key of the part to the last note played"));
    }
    if(seg == "OscilSmp" || seg == "FMSmp" || seg == "oscilgen") {
        params.push_back(action(n.path, "convert2sine",
            L"Convert to sine harmonics",
            L"Turns the current waveform into its harmonics on a sine base "
            L"function"));
        params.push_back(action(n.path, "use-as-base",
            L"Use as base function",
            L"Makes the current waveform the base function"));
    }
    if(seg == "padpars")
        params.push_back(action(n.path, "prepare",
            L"Apply changes (rebuild the PADsynth samples)",
            L"PADsynth settings take effect when its samples are rebuilt"));
    if(seg == "Reson" || seg == "resonance") {
        params.push_back(action(n.path, "smooth", L"Smooth the response",
                                L"Smooths out the frequency response"));
        params.push_back(action(n.path, "interpolatepeaks",
            L"Interpolate between peaks",
            L"Generates the response from its peak values", 0));
        params.push_back(action(n.path, "randomize", L"Randomize",
                                L"Randomizes the frequency response", 0));
        params.push_back(action(n.path, "zero", L"Reset the response",
                                L"Resets the frequency response to flat"));
    }
    if(seg == "microtonal") {
        Param t;
        t.name  = "tunings";
        t.path  = n.path + "tunings";
        t.type  = t.send = 's';
        t.multiline = true;
        t.label = L"Tunings, one step per line";
        t.doc   = L"Each line is one step of the scale: cents with a decimal "
                  L"point (like 100.0) or a ratio (like 3/2). The last line "
                  L"is the octave. Ctrl+Enter applies.";
        params.push_back(t);
        Param m = t;
        m.name  = "mapping";
        m.path  = n.path + "mapping";
        m.label = L"Key mapping, one key per line";
        m.doc   = L"Each line maps a key to a scale degree; x leaves the key "
                  L"silent. Used when mapping is enabled. Ctrl+Enter applies.";
        params.push_back(m);
    }
}

// ------------------------------------------------------------ sections

//! One section: its name and the settings it takes, as port names; a name
//! ending in '*' takes every port starting with it
struct SectionRule {
    const wchar_t *label;
    std::vector<const char *> ports;
};

static bool takes(const SectionRule &r, const std::string &name)
{
    for(const char *p : r.ports) {
        const size_t n = strlen(p);
        if(n && p[n - 1] == '*') {
            if(name.compare(0, n - 1, p, n - 1) == 0)
                return true;
        } else if(name == p)
            return true;
    }
    return false;
}

//! The rules for a group, chosen by the last part of its path
static const std::vector<SectionRule> *rulesFor(const std::string &nodePath)
{
    static const std::vector<SectionRule> padsynth = {
        {L"Output", {"PStereo", "PPanning", "PVolume", "Volume",
                     "PAmpVelocityScaleFunction", "Fadein_adjustment"}},
        {L"Punch", {"PPunch*"}},
        {L"Filter velocity", {"PFilterVelocity*"}},
        {L"Frequency", {"Pfixedfreq", "PfixedfreqET", "PBendAdjust",
                        "POffsetHz", "PDetune", "PCoarseDetune", "PDetuneType",
                        "octave", "coarsedetune"}},
        {L"Harmonic profile", {"Pmode", "Php.*"}},
        {L"Overtones", {"Phrpos.*"}},
        {L"Bandwidth", {"Pbandwidth", "Pbwscale"}},
        {L"Sample", {"Pquality.*", "prepare"}},
    };
    static const std::vector<SectionRule> addGlobal = {
        {L"Output", {"PStereo", "PPanning", "Volume", "PVolume",
                     "PAmpVelocityScaleFunction", "Fadein_adjustment",
                     "Hrandgrouping"}},
        {L"Punch", {"PPunch*"}},
        {L"Filter velocity", {"PFilterVelocity*"}},
        {L"Frequency", {"PDetune", "PCoarseDetune", "PDetuneType",
                        "PBandwidth", "octave", "coarsedetune"}},
    };
    static const std::vector<SectionRule> addVoice = {
        {L"Voice", {"Enabled", "Type", "PDelay", "Presonance", "PsyncEnabled"}},
        {L"Unison", {"Unison_*"}},
        {L"Modulator", {"PFM*", "FM*", "PextFMoscil", "PFMoscilphase"}},
        //not "Oscillator" or "Filter": the voice has groups of those names
        {L"Oscillator source", {"Pextoscil", "Poscilphase"}},
        {L"Frequency", {"Pfixedfreq", "PfixedfreqET", "PBendAdjust",
                        "POffsetHz", "PDetune", "PCoarseDetune", "PDetuneType",
                        "octave", "coarsedetune", "PFreqEnvelopeEnabled",
                        "PFreqLfoEnabled"}},
        {L"Amplitude", {"PPanning", "volume", "Volume", "PVolume",
                        "PVolumeminus", "PAAEnabled",
                        "PAmpVelocityScaleFunction", "PAmpEnvelopeEnabled",
                        "PAmpLfoEnabled"}},
        {L"Filter switches", {"PFilter*", "Pfilter*"}},
    };
    static const std::vector<SectionRule> subsynth = {
        {L"Output", {"Pstereo", "Volume", "PVolume", "PPanning",
                     "AmpVelocityScaleFunction", "PAmpVelocityScaleFunction"}},
        {L"Frequency", {"PDetune", "PCoarseDetune", "PDetuneType",
                        "Pfixedfreq", "PfixedfreqET", "PBendAdjust",
                        "POffsetHz", "octave", "coarsedetune",
                        "PFreqEnvelopeEnabled"}},
        {L"Filter switches", {"PGlobalFilter*"}},
        {L"Harmonics", {"Pnumstages", "Pbandwidth", "Phmagtype", "Phmag",
                        "Phrelbw", "Pbwscale", "Pstart",
                        "PBandWidthEnvelopeEnabled"}},
        {L"Overtones", {"POvertoneSpread.*"}},
    };
    static const std::vector<SectionRule> part = {
        {L"Part", {"Penabled", "Volume", "Pvolume", "Ppanning", "Pkeyshift",
                   "Prcvchn"}},
        {L"Keys and voices", {"Pkeylimit", "Pvoicelimit", "Pminkey",
                              "Pmaxkey", "Pnoteon", "Pvelsns", "Pveloffs",
                              "captureMin", "captureMax"}},
        {L"Modes", {"Pkitmode", "Pdrummode", "Ppolymode", "Plegatomode",
                    "polyType"}},
        {L"Instrument information", {"info.*", "Pname"}},
        {L"Effect routing", {"Pefxroute", "Pefxbypass"}},
    };
    static const std::vector<SectionRule> controllers = {
        {L"Pitch wheel", {"pitchwheel.*"}},
        {L"Modulation wheel", {"modwheel.*"}},
        {L"Portamento", {"portamento.*"}},
        {L"Controller depths", {"panning.*", "filtercutoff.*", "filterq.*",
                                "bandwidth.*", "resonancecenter.*",
                                "resonancebandwidth.*"}},
        {L"Controllers received", {"expression.receive", "fmamp.receive",
                                   "volume.receive", "sustain.receive",
                                   "NRPN.receive"}},
    };
    static const std::vector<SectionRule> oscillator = {
        {L"Base function", {"Pcurrentbasefunc", "Pbasefunc*", "use-as-base"}},
        {L"Harmonics", {"magnitude", "phase", "Phmagtype", "Pharmonicshift*",
                        "convert2sine"}},
        {L"Randomness", {"Prand", "Pamprand*"}},
        {L"Adaptive harmonics", {"Padaptiveharmonics*"}},
        {L"Waveshaping", {"Pwaveshaping*"}},
        {L"Filter", {"Pfilter*"}},
        {L"Spectrum adjust", {"Psatype", "Psapar"}},
        {L"Modulation", {"Pmodulation*"}},
    };
    static const std::vector<SectionRule> filter = {
        {L"Filter", {"Pcategory", "Ptype", "Pstages", "baseq", "basefreq",
                     "freqtracking", "gain", "type-*", "Pfreq", "Pq",
                     "Pfreqtrack", "Pgain"}},
        {L"Formants", {"Pnumformants", "Pformantslowness", "Pvowelclearness",
                       "Pcenterfreq", "Poctavesfreq"}},
        {L"Vowel sequence", {"Psequence*", "vowel_seq"}},
    };
    static const std::vector<SectionRule> settings = {
        {L"Audio", {"cfg.SampleRate", "cfg.SoundBufferSize", "cfg.OscilSize",
                    "cfg.OscilPower", "cfg.Interpolation", "cfg.SwapStereo",
                    "cfg.AudioOutputCompressor"}},
        {L"Files", {"cfg.GzipCompression", "cfg.SaveFullXml",
                    "cfg.CheckPADsynth"}},
    };
    static const std::vector<SectionRule> tuning = {
        {L"Scale", {"Penabled", "PAnote", "PAfreq", "Pscaleshift",
                    "Pinvertupdown", "Pinvertupdowncenter", "Pglobalfinedetune",
                    "Pname", "Pcomment", "tunings", "octavesize"}},
        {L"Keyboard mapping", {"Pfirstkey", "Plastkey", "Pmiddlenote",
                               "Pmapsize", "Pmappingenabled", "Pmapping",
                               "mapping"}},
    };

    std::string seg = nodePath;
    if(!seg.empty() && seg.back() == '/')
        seg.pop_back();
    seg = seg.substr(seg.rfind('/') + 1);
    std::string base = seg;
    while(!base.empty() && isdigit((unsigned char)base.back()))
        base.pop_back();

    if(base == "padpars")                      return &padsynth;
    if(base == "GlobalPar")                    return &addGlobal;
    if(base == "VoicePar")                     return &addVoice;
    if(base == "subpars")                      return &subsynth;
    if(base == "part" && nodePath.find('/', 1) == nodePath.size() - 1)
                                               return &part;
    if(base == "ctl")                          return &controllers;
    if(base == "OscilSmp" || base == "FMSmp" || base == "oscilgen")
                                               return &oscillator;
    if(base == "GlobalFilter" || base == "VoiceFilter" ||
       base == "filterpars")                   return &filter;
    if(base == "config")                       return &settings;
    if(base == "microtonal")                   return &tuning;
    return nullptr;
}

//! Larger groups than this get sections
constexpr size_t kSectionThreshold = 12;

std::vector<Section> sections(const std::string &nodePath,
                              const std::vector<Param> &params)
{
    std::vector<Section> out;
    const std::vector<SectionRule> *rules = rulesFor(nodePath);
    if(!rules || params.size() <= kSectionThreshold)
        return out;
    std::vector<Section> s(rules->size());
    Section other;
    other.label = L"Other";
    for(size_t i = 0; i < rules->size(); ++i)
        s[i].label = (*rules)[i].label;
    for(const Param &p : params) {
        bool placed = false;
        for(size_t i = 0; i < rules->size() && !placed; ++i)
            if(takes((*rules)[i], p.name)) {
                s[i].params.push_back(p);
                placed = true;
            }
        if(!placed)
            other.params.push_back(p);
    }
    for(Section &sec : s)
        if(!sec.params.empty())
            out.push_back(std::move(sec));
    if(!other.params.empty())
        out.push_back(std::move(other));
    if(out.size() < 2)
        out.clear(); //one section is no division
    return out;
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

    //Panning: 64 is the centre; some panners use 0 for "random". Not the
    //depth of the panning controller, nor "PAnote" (the note for A).
    const std::wstring wname = widen(p.name);
    if(int127 && contains(p.label + L" " + wname, L"panning") &&
       !contains(wname, L"depth") && !contains(p.label, L"depth")) {
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
    if(int127 && (p.name == "Pminkey" || p.name == "Pmaxkey" ||
                  p.name == "Pfirstkey" || p.name == "Plastkey" ||
                  p.name == "Pmiddlenote" || p.name == "PAnote" ||
                  p.name == "Pinvertupdowncenter"))
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
