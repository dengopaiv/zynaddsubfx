/*
  ZynAddSubFX - a software synthesizer

  PatchLoadCheck.cpp - Load one patch file and check that it plays

  Loads a .xiz, .xmz, .xsz or .xpz file with the same engine calls the
  application uses, then (for instruments and masters) plays a few notes and
  checks the output for silence and invalid samples. Prints one result line:

    OK|FAIL|WARN <tab> kind <tab> details

  Exit code: 0 ok, 1 load failure, 2 loaded but output is broken (NaN/inf).

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include "../Misc/Master.h"
#include "../Misc/Part.h"
#include "../Misc/Microtonal.h"
#include "../Misc/XMLwrapper.h"
#include "../Misc/Config.h"
#include "../Misc/Allocator.h"
#include "../Misc/MiddleWare.h"
#include "../DSP/FFTwrapper.h"
#include "../Synth/OscilGen.h"
#include "../Synth/Resonance.h"
#include "../Effects/EffectMgr.h"
#include "../Params/EnvelopeParams.h"
#include "../Params/LFOParams.h"
#include "../Params/FilterParams.h"
#include "../globals.h"
#include <rtosc/thread-link.h>

using namespace zyn;

char *instance_name = (char *)"";
MiddleWare *middleware;

static std::string lower_ext(const std::string &f)
{
    auto dot = f.find_last_of('.');
    if(dot == std::string::npos)
        return "";
    std::string e = f.substr(dot + 1);
    for(auto &c : e)
        c = (char)tolower((unsigned char)c);
    return e;
}

static double now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct Render {
    float peak_on = 0, peak_off = 0;
    long  bad = 0;
};

// Play notes on every channel, hold, release; measure the output.
static Render play(Master &m, const SYNTH_T &synth, const int *notes, int nnotes)
{
    Render r;
    // Queues like the app's middleware sets up; messages to the UI are dropped
    rtosc::ThreadLink bToU(4096 * 2 * 16, 1024 / 16), uToB(4096 * 2 * 16, 1024 / 16);
    m.bToU = &bToU;
    m.uToB = &uToB;
    float *l = new float[synth.buffersize];
    float *rr = new float[synth.buffersize];
    const int bufs_per_sec = synth.samplerate / synth.buffersize;

    auto run = [&](int bufs, float &peak) {
        for(int b = 0; b < bufs; ++b) {
            // The app's middleware answers /request-memory with 8MB chunks;
            // there is no middleware here, so top the pool up directly.
            while(m.memory->lowMemory(6, 1024 * 1024)) {
                const size_t N = 8 * 1024 * 1024;
                m.memory->addMemory(malloc(N), N);
            }
            m.AudioOut(l, rr);
            while(m.bToU && m.bToU->hasNext())
                m.bToU->read();
            for(int i = 0; i < synth.buffersize; ++i) {
                if(!std::isfinite(l[i]) || !std::isfinite(rr[i])) {
                    ++r.bad;
                    continue;
                }
                peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(rr[i])));
            }
        }
    };

    for(int ch = 0; ch < NUM_MIDI_CHANNELS; ++ch)
        for(int n = 0; n < nnotes; ++n)
            m.noteOn(ch, notes[n], 100);
    run(bufs_per_sec * 3 / 2, r.peak_on);
    for(int ch = 0; ch < NUM_MIDI_CHANNELS; ++ch)
        for(int n = 0; n < nnotes; ++n)
            m.noteOff(ch, notes[n]);
    run(bufs_per_sec, r.peak_off);

    m.bToU = nullptr;
    m.uToB = nullptr;
    delete[] l;
    delete[] rr;
    return r;
}

static int report(const char *kind, int load_rc, double load_s,
                  const Render *r, const std::string &extra)
{
    const char *status = "OK";
    int code = 0;
    std::string why;
    if(load_rc != 0) {
        status = "FAIL";
        code   = 1;
        why    = "load returned " + std::to_string(load_rc);
    } else if(r && r->bad) {
        status = "FAIL";
        code   = 2;
        why    = std::to_string(r->bad) + " NaN/inf samples";
    } else if(r && r->peak_on < 1e-5f) {
        status = "WARN";
        why    = "silent";
    } else if(r && r->peak_on > 4.0f) {
        status = "WARN";
        why    = "very loud";
    }
    printf("%s\t%s\tload=%.2fs", status, kind, load_s);
    if(r)
        printf("\tpeak=%.4f\trelease=%.4f", r->peak_on, r->peak_off);
    if(!why.empty())
        printf("\twhy=%s", why.c_str());
    printf("\t%s\n", extra.c_str());
    return code;
}

int main(int argc, char **argv)
{
    if(argc != 2) {
        fprintf(stderr, "usage: PatchLoadCheck <file.xiz|xmz|xsz|xpz>\n");
        return 64;
    }
    const std::string file = argv[1];
    const std::string ext  = lower_ext(file);

    SYNTH_T synth;
    synth.buffersize = 256;
    synth.samplerate = 48000;
    synth.alias();
    Config config;

    static const int notes[] = {36, 48, 60, 64, 67, 72, 84};
    const int nnotes = sizeof(notes) / sizeof(notes[0]);

    if(ext == "xiz") {
        Master m(synth, &config);
        Part *p = m.part[0];
        double t = now();
        int rc = p->loadXMLinstrument(file.c_str());
        if(rc == 0) {
            p->applyparameters();
            p->initialize_rt();
        }
        t = now() - t;
        if(rc != 0)
            return report("xiz", rc, t, nullptr, "");
        int kits = 0, ad = 0, sub = 0, pad = 0;
        for(int i = 0; i < NUM_KIT_ITEMS; ++i) {
            auto &k = p->kit[i];
            if(!(k.Penabled || i == 0))
                continue;
            if(i > 0 && p->Pkitmode == 0)
                continue;
            ++kits;
            ad  += k.Padenabled;
            sub += k.Psubenabled;
            pad += k.Ppadenabled;
        }
        Render r = play(m, synth, notes, nnotes);
        char extra[512];
        snprintf(extra, sizeof extra, "name=%s\tkits=%d ad=%d sub=%d pad=%d",
                 p->Pname, kits, ad, sub, pad);
        return report("xiz", 0, t, &r, extra);
    }

    if(ext == "xmz") {
        Master m(synth, &config);
        double t = now();
        int rc = m.loadXML(file.c_str());
        if(rc == 0)
            m.applyparameters();
        t = now() - t;
        if(rc != 0)
            return report("xmz", rc, t, nullptr, "");
        std::string parts;
        int enabled = 0;
        for(int i = 0; i < NUM_MIDI_PARTS; ++i)
            if(m.part[i]->Penabled) {
                ++enabled;
                if(!parts.empty())
                    parts += "; ";
                parts += std::to_string(i + 1) + ":" + m.part[i]->Pname;
            }
        Render r = play(m, synth, notes, nnotes);
        return report("xmz", 0, t, &r,
                      "parts=" + std::to_string(enabled) + "\t" + parts);
    }

    if(ext == "xsz") {
        Microtonal micro(0);
        double t = now();
        int rc = micro.loadXML(file.c_str());
        t = now() - t;
        char extra[512] = "";
        if(rc == 0)
            snprintf(extra, sizeof extra, "name=%s\tnotes=%d",
                     (const char *)micro.Pname, (int)micro.getoctavesize());
        return report("xsz", rc, t, nullptr, extra);
    }

    if(ext == "xpz") {
        // Name.Ptype.xpz: the type is the branch inside the file
        std::string base = file.substr(file.find_last_of("/\\") + 1);
        base = base.substr(0, base.size() - 4);
        std::string type = base.substr(base.find_last_of('.') + 1);
        double t = now();
        XMLwrapper xml;
        int rc = xml.loadXMLfile(file);
        if(rc == 0) {
            std::string branch = strstr(type.c_str(), "Plfo") ? "Plfo" : type;
            if(xml.enterbranch(branch) == 0)
                rc = -10;
        }
        if(rc == 0) {
            FFTwrapper fft(synth.oscilsize);
            if(type == "Poscilgen") {
                OscilGen o(synth, &fft, nullptr);
                o.getfromXML(xml);
            } else if(type == "Presonance") {
                Resonance res;
                res.getfromXML(xml);
            } else if(type == "Peffect") {
                EffectMgr e(DummyAlloc, synth, false);
                e.getfromXML(xml);
            } else if(strstr(type.c_str(), "Plfo")) {
                LFOParams lfo;
                lfo.getfromXML(xml);
            } else if(strstr(type.c_str(), "Penvamp") || strstr(type.c_str(), "Penv")) {
                EnvelopeParams env;
                env.getfromXML(xml);
            } else if(strstr(type.c_str(), "Pfilter")) {
                FilterParams fp;
                fp.getfromXML(xml);
            } else {
                t = now() - t;
                return report("xpz", 0, t, nullptr, "type=" + type + " (not checked beyond XML)");
            }
        }
        t = now() - t;
        return report("xpz", rc, t, nullptr, "type=" + type);
    }

    printf("SKIP\t%s\tnot a ZynAddSubFX patch\n", ext.c_str());
    return 0;
}
