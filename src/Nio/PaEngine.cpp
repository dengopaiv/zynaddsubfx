/*
  ZynAddSubFX - a software synthesizer

  PaEngine.cpp - Audio Driver for Portaudio
  Copyright (C) 2002 Nasca Octavian Paul
  Author: Nasca Octavian Paul

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/

#include "PaEngine.h"
#include "Nio.h"
#include <iostream>
#ifdef WIN32
#include <pa_win_wasapi.h>
#endif

using namespace std;

namespace zyn {

PaEngine::PaEngine(const SYNTH_T &synth)
    :AudioOut(synth), stream(NULL)
{
    name = "PA";
}


PaEngine::~PaEngine()
{
    Stop();
}

//! "Host API: device", the name a device is chosen by
static std::string deviceLabel(PaDeviceIndex i)
{
    const PaDeviceInfo  *dev = Pa_GetDeviceInfo(i);
    const PaHostApiInfo *api = dev ? Pa_GetHostApiInfo(dev->hostApi) : NULL;
    if(!dev)
        return std::string();
    return std::string(api ? api->name : "?") + ": " + dev->name;
}

std::vector<std::string> PaEngine::deviceNames()
{
    std::vector<std::string> names;
    const bool init = Pa_Initialize() == paNoError;
    if(!init)
        return names;
    const PaDeviceIndex n = Pa_GetDeviceCount();
    for(PaDeviceIndex i = 0; i < n; ++i) {
        const PaDeviceInfo *dev = Pa_GetDeviceInfo(i);
        if(dev && dev->maxOutputChannels >= 2)
            names.push_back(deviceLabel(i));
    }
    Pa_Terminate();
    return names;
}

//! The device to open: the one named by Nio::audioDevice, else the default
//! output of WASAPI (low latency, on Windows), else PortAudio's default
static PaDeviceIndex chooseDevice()
{
    const std::string &wanted = Nio::audioDevice;
    if(!wanted.empty()) {
        const PaDeviceIndex n = Pa_GetDeviceCount();
        for(PaDeviceIndex i = 0; i < n; ++i) {
            const PaDeviceInfo *dev = Pa_GetDeviceInfo(i);
            if(dev && dev->maxOutputChannels >= 2 && deviceLabel(i) == wanted)
                return i;
        }
        cerr << "[WARNING] Audio device \"" << wanted
             << "\" not found, using the default" << endl;
    }
#ifdef WIN32
    const PaHostApiIndex wasapi = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    if(wasapi >= 0) {
        const PaHostApiInfo *api = Pa_GetHostApiInfo(wasapi);
        if(api && api->defaultOutputDevice != paNoDevice)
            return api->defaultOutputDevice;
    }
#endif
    return Pa_GetDefaultOutputDevice();
}

bool PaEngine::openStream(PaDeviceIndex device)
{
    const PaDeviceInfo *info = Pa_GetDeviceInfo(device);
    if(!info)
        return false;

    PaStreamParameters outputParameters;
    outputParameters.device           = device;
    outputParameters.channelCount     = 2; /* stereo output */
    outputParameters.sampleFormat     = paFloat32; /* 32 bit floating point output */
    outputParameters.suggestedLatency = info->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

#ifdef WIN32
    //shared-mode WASAPI only runs at the mixer's rate: let Windows convert
    PaWasapiStreamInfo wasapiInfo = {};
    if(Pa_GetHostApiInfo(info->hostApi)->type == paWASAPI) {
        wasapiInfo.size            = sizeof(PaWasapiStreamInfo);
        wasapiInfo.hostApiType     = paWASAPI;
        wasapiInfo.version         = 1;
        wasapiInfo.flags           = paWinWasapiAutoConvert;
        outputParameters.hostApiSpecificStreamInfo = &wasapiInfo;
    }
#endif

    PaError err = Pa_OpenStream(&stream,
                                NULL,
                                &outputParameters,
                                synth.samplerate,
                                synth.buffersize,
                                0,
                                PAprocess,
                                (void *) this);
    if(err != paNoError) {
        cerr << "[WARNING] Could not open \"" << deviceLabel(device)
             << "\": " << Pa_GetErrorText(err) << endl;
        stream = NULL;
        return false;
    }
    err = Pa_StartStream(stream);
    if(err != paNoError) {
        cerr << "[WARNING] Could not start \"" << deviceLabel(device)
             << "\": " << Pa_GetErrorText(err) << endl;
        Pa_CloseStream(stream);
        stream = NULL;
        return false;
    }
    cerr << "[INFO] Audio output: " << deviceLabel(device) << endl;
    return true;
}

bool PaEngine::Start()
{
    if(getAudioEn())
        return true;
    if(Pa_Initialize() != paNoError)
        return false;

    const PaDeviceIndex device = chooseDevice();
    if(device == paNoDevice) {
        cerr << "Error: No default output device." << endl;
        Pa_Terminate();
        return false;
    }

    if(openStream(device))
        return true;

    //fall back to PortAudio's own default before giving up
    const PaDeviceIndex fallback = Pa_GetDefaultOutputDevice();
    if(fallback != paNoDevice && fallback != device && openStream(fallback))
        return true;

    Pa_Terminate();
    return false;
}


void PaEngine::setAudioEn(bool nval)
{
    if(nval)
        Start();
    else
        Stop();
}

bool PaEngine::getAudioEn() const
{
    return stream;
}

int PaEngine::PAprocess(const void *inputBuffer,
                        void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *outTime,
                        PaStreamCallbackFlags flags,
                        void *userData)
{
    (void) inputBuffer;
    (void) outTime;
    (void) flags;
    return static_cast<PaEngine *>(userData)->process((float *) outputBuffer,
                                                      framesPerBuffer);
}

int PaEngine::process(float *out, unsigned long framesPerBuffer)
{
    const Stereo<float *> smp = getNext();
    for(unsigned i = 0; i < framesPerBuffer; ++i) {
        *out++ = smp.l[i];
        *out++ = smp.r[i];
    }

    return 0;
}

void PaEngine::Stop()
{
    if(!getAudioEn())
        return;
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    stream = NULL;
    Pa_Terminate();
}

}
