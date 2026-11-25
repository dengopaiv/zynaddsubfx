/*
  ZynAddSubFX - a software synthesizer

  Access.cpp - Screen reader support for standard Windows controls

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#include "Access.h"

#include <initguid.h> //defines the oleacc and UI Automation GUIDs used here
#include <oleacc.h>
#include <uiautomation.h>
#include <map>

namespace zynui {
namespace access {

static IAccPropServices *gAcc  = nullptr;
static HWND              gMain = nullptr;

static std::map<HWND, std::wstring> gNames; //names as set, to restore
static std::map<HWND, std::function<std::wstring(HWND)>> gValues;
static HWND gBorrowed = nullptr; //control whose name carries an announcement
constexpr UINT_PTR kAnnounceTimer = 0x5A1;

//! Answers PROPID_ACC_VALUE for the controls registered with setValueText
class ValueServer:public IAccPropServer
{
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
        {
            if(riid == IID_IUnknown || riid == IID_IAccPropServer) {
                *ppv = static_cast<IAccPropServer *>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return (ULONG)InterlockedIncrement(&ref);
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            LONG r = InterlockedDecrement(&ref);
            if(!r)
                delete this;
            return (ULONG)r;
        }
        HRESULT STDMETHODCALLTYPE GetPropValue(const BYTE *id, DWORD idLen,
                                               MSAAPROPID prop, VARIANT *out,
                                               BOOL *has) override
        {
            *has = FALSE;
            VariantInit(out);
            HWND  h     = nullptr;
            DWORD obj   = 0, child = 0;
            if(!gAcc || prop != PROPID_ACC_VALUE ||
               FAILED(gAcc->DecomposeHwndIdentityString(id, idLen, &h, &obj,
                                                         &child)) ||
               child != (DWORD)CHILDID_SELF)
                return S_OK;
            auto it = gValues.find(h);
            if(it == gValues.end())
                return S_OK;
            std::wstring t = it->second(h);
            if(t.empty())
                return S_OK;
            out->vt      = VT_BSTR;
            out->bstrVal = SysAllocString(t.c_str());
            *has         = TRUE;
            return S_OK;
        }
    private:
        LONG ref = 1;
};
static ValueServer *gServer = nullptr;

void init(HWND mainWindow)
{
    gMain = mainWindow;
    if(!gAcc)
        CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IAccPropServices, (void **)&gAcc);
    if(!gServer)
        gServer = new ValueServer();
}

void shutdown()
{
    gValues.clear();
    gNames.clear();
    if(gServer) {
        gServer->Release();
        gServer = nullptr;
    }
    if(gAcc) {
        gAcc->Release();
        gAcc = nullptr;
    }
}

void setName(HWND h, const std::wstring &name)
{
    if(!h)
        return;
    gNames[h] = name;
    if(!gAcc || h == gBorrowed)
        return;
    gAcc->SetHwndPropStr(h, (DWORD)OBJID_CLIENT, (DWORD)CHILDID_SELF,
                         PROPID_ACC_NAME, name.c_str());
}

void setDescription(HWND h, const std::wstring &text)
{
    if(!gAcc || !h || text.empty())
        return;
    gAcc->SetHwndPropStr(h, (DWORD)OBJID_CLIENT, (DWORD)CHILDID_SELF,
                         PROPID_ACC_DESCRIPTION, text.c_str());
}

void forget(HWND h)
{
    if(h == gBorrowed)
        gBorrowed = nullptr;
    gNames.erase(h);
    gValues.erase(h);
    if(gAcc) {
        MSAAPROPID props[] = {PROPID_ACC_NAME, PROPID_ACC_DESCRIPTION,
                              PROPID_ACC_VALUE};
        gAcc->ClearHwndProps(h, (DWORD)OBJID_CLIENT, (DWORD)CHILDID_SELF,
                             props, 3);
    }
}

void setValueText(HWND h, std::function<std::wstring(HWND)> text)
{
    if(!h)
        return;
    gValues[h] = std::move(text);
    if(!gAcc || !gServer)
        return;
    MSAAPROPID props[] = {PROPID_ACC_VALUE};
    gAcc->SetHwndPropServer(h, (DWORD)OBJID_CLIENT, (DWORD)CHILDID_SELF,
                            props, 1, gServer, ANNO_THIS);
}

void valueChanged(HWND h)
{
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, h, OBJID_CLIENT, CHILDID_SELF);
}

static void restoreBorrowed()
{
    if(!gBorrowed)
        return;
    HWND h    = gBorrowed;
    gBorrowed = nullptr;
    auto it   = gNames.find(h);
    if(!gAcc)
        return;
    if(it != gNames.end())
        gAcc->SetHwndPropStr(h, (DWORD)OBJID_CLIENT, (DWORD)CHILDID_SELF,
                             PROPID_ACC_NAME, it->second.c_str());
    else {
        MSAAPROPID props[] = {PROPID_ACC_NAME};
        gAcc->ClearHwndProps(h, (DWORD)OBJID_CLIENT, (DWORD)CHILDID_SELF,
                             props, 1);
    }
}

void announce(const std::wstring &text)
{
    if(text.empty() || !gMain)
        return;

    //UI Automation notification: Narrator, JAWS
    IRawElementProviderSimple *prov = nullptr;
    if(SUCCEEDED(UiaHostProviderFromHwnd(gMain, &prov)) && prov) {
        BSTR t = SysAllocString(text.c_str());
        BSTR a = SysAllocString(L"zyn-status");
        UiaRaiseNotificationEvent(prov, NotificationKind_ActionCompleted,
                                  NotificationProcessing_ImportantMostRecent,
                                  t, a);
        SysFreeString(t);
        SysFreeString(a);
        prov->Release();
    }

    //NVDA: a name change of the focused control is read out. The name
    //becomes "<message>, <own name>" and goes back 1.5 s later, silently.
    HWND f = GetFocus();
    if(!gAcc || !f || (f != gMain && !IsChild(gMain, f)))
        return;
    restoreBorrowed();
    std::wstring own;
    auto it = gNames.find(f);
    if(it != gNames.end())
        own = it->second;
    else {
        wchar_t b[256] = L"";
        GetWindowTextW(f, b, 256);
        for(wchar_t c : std::wstring(b))
            if(c != L'&')
                own += c;
    }
    std::wstring name = own.empty() ? text : text + L", " + own;
    gAcc->SetHwndPropStr(f, (DWORD)OBJID_CLIENT, (DWORD)CHILDID_SELF,
                         PROPID_ACC_NAME, name.c_str());
    gBorrowed = f;
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, f, OBJID_CLIENT, CHILDID_SELF);
    SetTimer(gMain, kAnnounceTimer, 1500, nullptr);
}

bool onTimer(WPARAM id)
{
    if(id != kAnnounceTimer)
        return false;
    KillTimer(gMain, kAnnounceTimer);
    restoreBorrowed();
    return true;
}

}
}
