/*
  ZynAddSubFX - a software synthesizer

  Win32Ui.cpp - The accessible user interface: standard Windows controls

  One window: a menu bar, a tree of the synthesizer's structure (Master,
  parts, kit items, engines, voices, envelopes, effects ...) and, for the
  group selected in the tree, a pane with one standard control per
  parameter. Both are built from the engine's port metadata (PortModel).
  F6 moves between the tree and the pane.

  The UI talks to the engine only through MiddleWare, with OSC messages,
  like every other zyn interface: it sends with transmitMsgGui and is
  called back through GUI::raiseUi. It runs on the main thread, which
  also ticks MiddleWare (main.cpp), so nothing here touches the audio
  thread.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#include "../Connection.h"
#include "../Fl_Osc_Interface.h"
#include "../../Misc/MiddleWare.h"
#include "../../Nio/Nio.h"
#include "zyn-version.h"

#include "Access.h"
#include "PortModel.h"
#include "resource.h"

#include <rtosc/rtosc.h>

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <cstdarg>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace zynui;

namespace {

// ---------------------------------------------------------------- state

HINSTANCE        gInst   = nullptr;
zyn::MiddleWare *gMw     = nullptr;
int             *gExit   = nullptr;
HWND gMain = nullptr, gTreeLabel = nullptr, gTree = nullptr, gView = nullptr,
     gCanvas = nullptr, gStatus = nullptr;
HFONT gFont  = nullptr;
UINT  gDpi   = 96;
HACCEL gAccel = nullptr;
HWND  gSavedFocus = nullptr; //focus when the window was deactivated
HWND  gLastFocus  = nullptr; //to scroll a newly focused control into view
int   gScrollY = 0, gCanvasH = 0;

std::deque<Node> gNodes; //tree items point into this (lParam = index)
Node             gPaneNode;

enum Kind { K_SLIDER, K_COMBO, K_CHECK, K_EDIT, K_MEMO, K_BUTTON, K_INDEX,
            K_HEADING };

struct Ctl {
    Param  p;
    Kind   kind  = K_SLIDER;
    HWND   label = nullptr, ctl = nullptr;
    double value = 0;     //last value known (engine units)
    bool   known = false; //a reply has arrived
    int    lastSentPos = -1;
    Ctl   *target = nullptr; //K_INDEX: the control showing the chosen element
    std::string base;        //an array element's path without its number
};
std::vector<std::unique_ptr<Ctl>> gCtls;
std::map<std::string, Ctl *>      gByPath;
std::map<HWND, Ctl *>             gByHwnd;

constexpr int kFirstCtlId = 1000;
constexpr int kParts      = 16;
constexpr int kBankSlots  = 160;

//The parts' tree items, named after their instruments
HTREEITEM    gPartItem[kParts]    = {};
std::wstring gPartName[kParts];
bool         gPartEnabled[kParts] = {true};

//Every tree item by its node's key (nodeKey)
std::map<std::string, HTREEITEM> gItemByPath;

//! A node's identity: its path, and the section for a section of a group
std::string nodeKey(const Node &n)
{
    return n.section.empty() ? n.path : n.path + "\x01" + narrow(n.section);
}

//The control last focused in each group (by node key): its path
std::map<std::string, std::string> gFocusMemo;

//The pane's heading, and for a pane inside an engine (AddSynth, SubSynth,
//PADsynth) the kit item's switch for that engine: an engine that is off
//answers no questions, so its settings would all read 0
HWND         gHeading = nullptr;
std::wstring gHeadingText;
std::string  gEngineSwitch;   //e.g. "/part0/kit0/Ppadenabled", or ""
std::wstring gEngineName;     //"PADsynth"
bool         gEngineOff = false;

std::wstring engineOffNote()
{
    return gEngineName + L" is off in this kit item, so its settings show "
           L"no values. Switch it on with \"" +
           (gEngineName == L"AddSynth" ? std::wstring(L"ADsynth")
            : gEngineName == L"SubSynth" ? std::wstring(L"SUBsynth")
                                         : gEngineName) +
           L" enable\" in the kit item.";
}

//Effect slots: their current effect type (EffectMgr's efftype)
std::map<std::string, int> gEffType;
constexpr int kDynFilterType = 8;

//! "/sysefx0/", "/insefx3/", "/part0/partefx1/"
bool isEffectNode(const std::string &path)
{
    if(path.size() < 6 || path.back() != '/')
        return false;
    size_t i = path.size() - 1;
    size_t d = i;
    while(d > 0 && isdigit((unsigned char)path[d - 1]))
        --d;
    return d < i && d >= 3 && path.compare(d - 3, 3, "efx") == 0;
}

//! The port group of an effect type, as EffectMgr names its subtypes
std::string effectGroup(int type)
{
    static const char *groups[] = {"", "Reverb", "Echo", "Chorus", "Phaser",
        "Alienwah", "Distortion", "EQ", "DynamicFilter", "Sympathetic",
        "Reverse"};
    return type > 0 && type < 11 ? groups[type] : "";
}

std::wstring effectName(int type)
{
    static const wchar_t *names[] = {L"off", L"Reverb", L"Echo", L"Chorus",
        L"Phaser", L"AlienWah", L"Distortion", L"EQ", L"Dynamic filter",
        L"Sympathetic", L"Reverse"};
    return type >= 0 && type < 11 ? names[type] : L"effect " + std::to_wstring(type);
}

//! A node's name in the tree: an effect slot with its effect type
std::wstring nodeLabel(const Node &n)
{
    if(isEffectNode(n.path)) {
        auto t = gEffType.find(n.path);
        if(t != gEffType.end())
            return t->second == 0 ? n.label + L", off"
                                  : n.label + L": " + effectName(t->second);
    }
    return n.label;
}

void refreshEffect(const std::string &path);

//Bank browser (a dialog that stays open)
HWND gBankDlg = nullptr;
struct Slot { std::string name, file; };
std::vector<std::pair<std::string, std::string>> gBanks; //name, folder
Slot gSlots[kBankSlots];
int  gBankPos      = -1;
bool gSlotsChanged = false;
int  gPreviewNote  = -1, gPreviewChan = 0;
//what Space last loaded, so holding it again does not load it again
int  gPreviewSlot = -1, gPreviewPart = -1, gPreviewBank = -1;

//The held note (F5): one note that sounds until F5 again, to listen while
//editing. Its note and velocity are kept in the interface's own settings.
int  gHoldNote = 48, gHoldVelocity = 100; //C3
bool gHoldOn   = false;
int  gHoldChan = 0, gHoldPlaying = 48;    //what is sounding now

//Recording to a WAV file (the synthesizer's HDD recorder)
bool         gRecording = false;
std::wstring gRecordFile;

//Playing from the computer keyboard
HWND gPlayWnd     = nullptr;
int  gPlayOctave  = 4, gPlayVelocity = 100;
std::map<WPARAM, std::pair<int, int>> gHeld; //key -> channel, note

enum {
    IDM_NEW = 40001, IDM_OPEN, IDM_SAVE, IDM_OPEN_INSTR, IDM_SAVE_INSTR,
    IDM_EXIT, IDM_UNDO, IDM_REDO, IDM_SETTINGS, IDM_HELP_KEYS, IDM_ABOUT,
    IDM_SWITCH_PANE, IDM_FOCUS_TREE, IDM_BANK, IDM_PLAY, IDM_PREV_GROUP,
    IDM_NEXT_GROUP, IDM_FIND, IDM_NEXT_SECTION, IDM_PREV_SECTION,
    IDM_OPEN_SCL, IDM_OPEN_KBM, IDM_OPEN_XSZ, IDM_SAVE_XSZ,
    IDM_HOLD_NOTE, IDM_HOLD_NOTE_SETTINGS, IDM_RECORD,
};

int px(int v) { return MulDiv(v, (int)gDpi, 96); }

//! With ZYN_UI_TRACE=<file> set, what the UI sends and receives is logged
FILE *traceFile()
{
    static FILE *f = nullptr;
    static bool tried = false;
    if(!tried) {
        tried = true;
        if(const char *name = getenv("ZYN_UI_TRACE"))
            f = fopen(name, "w");
    }
    return f;
}

void trace(const char *dir, const char *msg)
{
    if(FILE *f = traceFile()) {
        fprintf(f, "%s %s %s\n", dir, msg, rtosc_argument_string(msg));
        fflush(f);
    }
}

// ---------------------------------------------------------- messaging

void send(const char *path, const char *args, ...)
{
    if(!gMw)
        return;
    char buffer[2048];
    va_list va;
    va_start(va, args);
    size_t len = rtosc_vmessage(buffer, sizeof(buffer), path, args, va);
    va_end(va);
    if(!len)
        return;
    trace(">", buffer);
    gMw->transmitMsgGui(0, buffer);
}

void request(const std::string &path)
{
    send(path.c_str(), "");
}

void status(const std::wstring &text)
{
    if(gStatus)
        SendMessageW(gStatus, SB_SETTEXTW, 0, (LPARAM)text.c_str());
}

void say(const std::wstring &text)
{
    status(text);
    access::announce(text);
}

int currentPart()
{
    int p = partOfPath(gPaneNode.path);
    return p < 0 ? 0 : p;
}

// -------------------------------------------------- values <-> controls

int positions(const Param &p)
{
    if(p.type == 'f')
        return 200;
    int n = (int)std::lround(p.max - p.min);
    return n > 0 ? n : 127;
}

double posToValue(const Param &p, int pos)
{
    const int n = positions(p);
    if(p.type != 'f')
        return p.min + pos;
    const double t = (double)pos / n;
    if(p.logScale) {
        double lo = p.min > 0 ? p.min : (p.logMin > 0 ? p.logMin : 0);
        if(lo > 0 && p.max > lo) {
            if(pos == 0 && p.min <= 0)
                return p.min;
            return lo * std::pow(p.max / lo, t);
        }
    }
    return p.min + (p.max - p.min) * t;
}

int valueToPos(const Param &p, double v)
{
    const int n = positions(p);
    int pos;
    if(p.type != 'f')
        pos = (int)std::lround(v - p.min);
    else {
        double t;
        double lo = p.min > 0 ? p.min : (p.logMin > 0 ? p.logMin : 0);
        if(p.logScale && lo > 0 && p.max > lo)
            t = v <= lo ? 0 : std::log(v / lo) / std::log(p.max / lo);
        else
            t = p.max != p.min ? (v - p.min) / (p.max - p.min) : 0;
        pos = (int)std::lround(t * n);
    }
    return pos < 0 ? 0 : (pos > n ? n : pos);
}

std::wstring ctlValueText(const Ctl &c)
{
    if(c.kind == K_INDEX) {
        const int pos = (int)SendMessageW(c.ctl, TBM_GETPOS, 0, 0);
        return c.p.label.substr(0, c.p.label.find(L" number")) + L" " +
               std::to_wstring(pos + 1) + L" of " +
               std::to_wstring((long long)std::lround(c.p.max) + 1);
    }
    if(c.kind == K_SLIDER) {
        const int pos = (int)SendMessageW(c.ctl, TBM_GETPOS, 0, 0);
        //the exact value while the slider sits at its position
        if(c.known && valueToPos(c.p, c.value) == pos)
            return valueText(c.p, c.value);
        return valueText(c.p, posToValue(c.p, pos));
    }
    return valueText(c.p, c.value);
}

void showValue(Ctl &c)
{
    switch(c.kind) {
        case K_SLIDER:
            SendMessageW(c.ctl, TBM_SETPOS, TRUE, valueToPos(c.p, c.value));
            break;
        case K_COMBO: {
            const int n = (int)SendMessageW(c.ctl, CB_GETCOUNT, 0, 0);
            for(int i = 0; i < n; ++i)
                if((int)SendMessageW(c.ctl, CB_GETITEMDATA, i, 0) ==
                   (int)std::lround(c.value)) {
                    SendMessageW(c.ctl, CB_SETCURSEL, i, 0);
                    break;
                }
            break;
        }
        case K_CHECK:
            SendMessageW(c.ctl, BM_SETCHECK,
                         c.value != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        default:
            break;
    }
    if(GetFocus() == c.ctl)
        access::valueChanged(c.ctl);
}

void sendValue(Ctl &c, double v)
{
    c.value = v;
    c.known = true;
    const char *path = c.p.path.c_str();
    switch(c.p.send) {
        case 'i': send(path, "i", (int)std::lround(v)); break;
        case 'c': send(path, "c", (int)std::lround(v)); break;
        case 'f': send(path, "f", (double)v); break;
        case 'T': send(path, v != 0 ? "T" : "F"); break;
        default: break;
    }
}

void sendText(Ctl &c)
{
    const int n = GetWindowTextLengthW(c.ctl);
    std::wstring buf(n + 1, L'\0');
    GetWindowTextW(c.ctl, &buf[0], n + 1);
    buf.resize(n);
    std::string t = narrow(buf);
    //the engine separates lines with a line feed only
    t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
    send(c.p.path.c_str(), "s", t.c_str());
}

void runAction(Ctl &c)
{
    if(c.p.actionArg >= 0)
        send(c.p.path.c_str(), "i", c.p.actionArg);
    else
        send(c.p.path.c_str(), "");
    say(c.p.label + L": done");
}

double defaultOf(const Param &p, bool *ok)
{
    *ok = false;
    const std::string &d = p.defaultValue;
    if(d.empty())
        return 0;
    if(d == "true" || d == "T") { *ok = true; return 1; }
    if(d == "false" || d == "F") { *ok = true; return 0; }
    char  *end = nullptr;
    double v   = strtod(d.c_str(), &end);
    if(end != d.c_str()) {
        *ok = true;
        return v;
    }
    for(auto &o : p.options) //a default given as a choice's name
        if(narrow(o.second) == d) {
            *ok = true;
            return o.first;
        }
    return 0;
}

// ------------------------------------------------------------- the pane

void layoutCanvas();

void clearPane()
{
    for(auto &c : gCtls) {
        access::forget(c->ctl);
        DestroyWindow(c->ctl);
        if(c->label)
            DestroyWindow(c->label);
    }
    gCtls.clear();
    gByPath.clear();
    gByHwnd.clear();
    //anything else on the canvas (the heading)
    while(HWND h = GetWindow(gCanvas, GW_CHILD))
        DestroyWindow(h);
    gHeading = nullptr;
}

HWND makeChild(const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             0, 0, 10, 10, gCanvas, (HMENU)(INT_PTR)id,
                             gInst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)gFont, FALSE);
    return h;
}

std::wstring nodeTitle(HTREEITEM item)
{
    std::wstring t;
    while(item) {
        wchar_t buf[256] = L"";
        TVITEMW it = {};
        it.mask       = TVIF_TEXT;
        it.hItem      = item;
        it.pszText    = buf;
        it.cchTextMax = 256;
        //TVM_GETITEMW itself: the TreeView_GetItem macro asks for ANSI text
        //in a build without UNICODE defined
        SendMessageW(gTree, TVM_GETITEMW, 0, (LPARAM)&it);
        t     = t.empty() ? std::wstring(buf) : std::wstring(buf) + L", " + t;
        item  = TreeView_GetParent(gTree, item);
    }
    return t;
}

Ctl *addControl(const Param &p, Kind kind, int id);

//! A control's identity within its group: its path below the group's (an
//! array's value control by the array, whichever element it shows). The same
//! setting of another voice or part has the same key.
std::string ctlKey(const Ctl &c)
{
    const std::string p  = c.base.empty() ? c.p.path : c.base;
    const std::string &np = gPaneNode.path;
    return p.compare(0, np.size(), np) == 0 ? p.substr(np.size()) : p;
}

Ctl *findCtl(const std::string &key)
{
    for(auto &c : gCtls)
        if(c->kind != K_HEADING && ctlKey(*c) == key)
            return c.get();
    return nullptr;
}

Ctl *firstCtl()
{
    for(auto &c : gCtls)
        if(c->kind != K_HEADING)
            return c.get();
    return nullptr;
}

//! Into the pane: the control last used in this group, else the first
bool focusPane()
{
    auto m = gFocusMemo.find(nodeKey(gPaneNode));
    Ctl *c = m != gFocusMemo.end() ? findCtl(m->second) : nullptr;
    if(!c)
        c = firstCtl();
    if(!c)
        return false;
    SetFocus(c->ctl);
    return true;
}

void buildPane(const Node &n, const std::wstring &title)
{
    //the same pane built again (its effect type changed): keep the focus
    std::string focusPath;
    if(nodeKey(n) == nodeKey(gPaneNode)) {
        HWND f = GetFocus();
        if(f && !gByHwnd.count(f) && GetParent(f))
            f = GetParent(f); //the edit part of a combo box
        auto fc = gByHwnd.find(f);
        if(fc != gByHwnd.end())
            focusPath = ctlKey(*fc->second);
    }

    SendMessageW(gCanvas, WM_SETREDRAW, FALSE, 0);
    clearPane();
    gPaneNode = n;

    std::vector<Param> ps = params(n);
    if(isEffectNode(n.path)) {
        //an effect slot's preset: the names its current effect type gives
        auto t = gEffType.find(n.path);
        for(size_t i = 0; i < ps.size(); ++i) {
            if(ps[i].name != "preset")
                continue;
            if(t == gEffType.end())
                break; //type not known yet: a plain number until it arrives
            auto names = groupPortOptions(n, effectGroup(t->second), "preset");
            if(names.empty()) {
                ps.erase(ps.begin() + i); //off, or a type without presets
                break;
            }
            ps[i].options = names;
            ps[i].min     = names.front().first;
            ps[i].max     = names.back().first;
            ps[i].label   = effectName(t->second) + L" preset";
            //the effect type's own port: it answers when asked, the slot's
            //does not (so loaded files show their preset)
            ps[i].path    = n.path + effectGroup(t->second) + "/preset";
            break;
        }
    }
    //a large group shown whole: its settings under section headings
    //(Ctrl+Down and Ctrl+Up jump between them); the sections are also the
    //group's first children in the tree
    std::vector<Section> secs;
    if(n.section.empty() && !isEffectNode(n.path))
        secs = sections(n);
    struct Item { bool heading; std::wstring label; Param p; };
    std::vector<Item> items;
    if(!secs.empty()) {
        for(Section &sec : secs) {
            items.push_back({true, sec.label, Param()});
            for(Param &p : sec.params)
                items.push_back({false, L"", p});
        }
    } else
        for(Param &p : ps)
            items.push_back({false, L"", p});

    std::wstring head = title;
    if(ps.empty())
        head += L": no settings of its own. Open it in the tree to see its parts.";
    else if(!secs.empty())
        head += L": " + std::to_wstring(secs.size()) + L" sections. Ctrl+Down "
                L"and Ctrl+Up jump between them; they are also in the tree.";
    gHeadingText = head;
    gHeading = makeChild(L"STATIC", head.c_str(), SS_LEFT | SS_NOPREFIX, -1);

    //inside an engine: is it switched on in its kit item?
    gEngineSwitch.clear();
    gEngineOff = false;
    {
        static const std::pair<const char *, const wchar_t *> engines[] = {
            {"/adpars/", L"AddSynth"}, {"/subpars/", L"SubSynth"},
            {"/padpars/", L"PADsynth"}};
        static const char *switches[] = {"Padenabled", "Psubenabled",
                                         "Ppadenabled"};
        for(int e = 0; e < 3; ++e) {
            size_t at = n.path.find(engines[e].first);
            if(at != std::string::npos &&
               n.path.compare(0, 5, "/part") == 0) {
                gEngineSwitch = n.path.substr(0, at + 1) + switches[e];
                gEngineName   = engines[e].second;
                request(gEngineSwitch);
                break;
            }
        }
    }

    int id = kFirstCtlId;
    for(Item &item : items) {
        if(item.heading) {
            Param hp;
            hp.label = item.label;
            hp.path  = "#section:" + narrow(item.label);
            addControl(hp, K_HEADING, -1);
            continue;
        }
        Param &p = item.p;
        if(p.count > 0) {
            //an array: which element, then its value
            Param ip;
            ip.name  = p.name + "#";
            ip.path  = p.path + "#";
            ip.type  = 'i';
            ip.min   = 0;
            ip.max   = p.count - 1;
            ip.label = p.indexName + L" number, for " + p.label;
            ip.doc   = L"Chooses which " + p.indexName + L" the next control "
                       L"edits";
            Ctl *index = addControl(ip, K_INDEX, id++);
            Param vp = p;
            vp.path  = p.path + "0" + p.indexSuffix;
            vp.label = p.label + L", " + p.indexName + L" 1";
            //each element is a slider, or a list or check box when the
            //elements have named choices or are on/off
            const Kind vk = !vp.options.empty() ? K_COMBO
                            : vp.type == 'T'    ? K_CHECK
                                                : K_SLIDER;
            Ctl *value = addControl(vp, vk, id++);
            index->target = value;
            value->base   = p.path;
            index->known  = true;
            continue;
        }
        Kind k = K_SLIDER;
        if(p.action)
            k = K_BUTTON;
        else if(!p.options.empty())
            k = K_COMBO;
        else if(p.type == 'T')
            k = K_CHECK;
        else if(p.type == 's')
            k = p.multiline ? K_MEMO : K_EDIT;
        addControl(p, k, id++);
    }

    gScrollY = 0;
    layoutCanvas();
    SendMessageW(gCanvas, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gCanvas, nullptr, TRUE);

    for(auto &c : gCtls)
        if(c->kind != K_BUTTON && c->kind != K_INDEX && c->kind != K_HEADING)
            request(c->p.path);

    if(!focusPath.empty())
        if(Ctl *c = findCtl(focusPath))
            SetFocus(c->ctl);
}

Ctl *addControl(const Param &p, Kind kind, int id)
{
    auto c  = std::make_unique<Ctl>();
    c->p    = p;
    c->kind = kind;
    if(kind == K_HEADING) {
        //a section heading: text only, never focused
        c->ctl = makeChild(L"STATIC", (L"Section: " + p.label).c_str(),
                           SS_LEFT | SS_NOPREFIX, -1);
        Ctl *raw = c.get();
        gCtls.push_back(std::move(c));
        return raw;
    }
    if(kind != K_CHECK && kind != K_BUTTON)
        c->label = makeChild(L"STATIC", (p.label + L":").c_str(),
                             SS_LEFT | SS_NOPREFIX, -1);
    switch(kind) {
        case K_SLIDER:
        case K_INDEX:
            c->ctl = makeChild(TRACKBAR_CLASSW, L"",
                               WS_TABSTOP | TBS_HORZ | TBS_NOTICKS |
                               TBS_DOWNISLEFT, id);
            SendMessageW(c->ctl, TBM_SETRANGEMIN, FALSE, 0);
            SendMessageW(c->ctl, TBM_SETRANGEMAX, FALSE, positions(p));
            SendMessageW(c->ctl, TBM_SETLINESIZE, 0, 1);
            SendMessageW(c->ctl, TBM_SETPAGESIZE, 0,
                         positions(p) >= 20 ? positions(p) / 10 : 1);
            {
                Ctl *raw = c.get();
                access::setValueText(c->ctl, [raw](HWND) {
                    return ctlValueText(*raw);
                });
            }
            break;
        case K_COMBO:
            c->ctl = makeChild(WC_COMBOBOXW, L"",
                               WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id);
            for(auto &o : p.options) {
                int i = (int)SendMessageW(c->ctl, CB_ADDSTRING, 0,
                                          (LPARAM)o.second.c_str());
                SendMessageW(c->ctl, CB_SETITEMDATA, i, o.first);
            }
            break;
        case K_CHECK:
            c->ctl = makeChild(WC_BUTTONW, p.label.c_str(),
                               WS_TABSTOP | BS_AUTOCHECKBOX, id);
            break;
        case K_BUTTON:
            c->ctl = makeChild(WC_BUTTONW, p.label.c_str(),
                               WS_TABSTOP | BS_PUSHBUTTON, id);
            break;
        case K_EDIT:
            c->ctl = makeChild(WC_EDITW, L"",
                               WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, id);
            break;
        case K_MEMO:
            c->ctl = makeChild(WC_EDITW, L"",
                               WS_TABSTOP | WS_BORDER | WS_VSCROLL |
                               ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                               id);
            break;
    }
    access::setName(c->ctl, p.label);
    access::setDescription(c->ctl, p.doc);
    Ctl *raw = c.get();
    if(kind != K_BUTTON && kind != K_INDEX)
        gByPath[p.path] = raw;
    gByHwnd[c->ctl] = raw;
    gCtls.push_back(std::move(c));
    return raw;
}

//! An array's element chooser moved: the value control edits that element
void chooseElement(Ctl &index, int i)
{
    index.value = i;
    Ctl *t = index.target;
    if(!t)
        return;
    gByPath.erase(t->p.path);
    t->p.path = t->base + std::to_string(i) + t->p.indexSuffix;
    t->known  = false;
    gByPath[t->p.path] = t;
    const std::wstring name = t->p.label.substr(0, t->p.label.rfind(L", ")) +
                              L", " + index.p.label.substr(0, index.p.label.find(L" number")) +
                              L" " + std::to_wstring(i + 1);
    t->p.label = name;
    access::setName(t->ctl, name);
    if(t->label)
        SetWindowTextW(t->label, (name + L":").c_str());
    else //a check box is its own label
        SetWindowTextW(t->ctl, name.c_str());
    request(t->p.path);
}

void layoutCanvas()
{
    RECT vr;
    GetClientRect(gView, &vr);
    const int w      = vr.right - vr.left;
    const int margin = px(8), rowH = px(26), labelW = (w - 3 * margin) * 45 / 100;
    const int ctlX   = margin * 2 + labelW, ctlW = w - ctlX - margin;
    int y = margin;

    HWND heading = GetWindow(gCanvas, GW_CHILD);
    //the heading is the first window that is not a control's label
    for(HWND h = heading; h; h = GetWindow(h, GW_HWNDNEXT)) {
        bool owned = false;
        for(auto &c : gCtls)
            if(c->label == h || c->ctl == h)
                owned = true;
        if(!owned) {
            MoveWindow(h, margin, y, w - 2 * margin, px(40), FALSE);
            y += px(46);
            break;
        }
    }
    for(auto &c : gCtls) {
        if(c->kind == K_HEADING) {
            y += px(6);
            MoveWindow(c->ctl, margin, y, w - 2 * margin, px(22), FALSE);
        } else if(c->kind == K_CHECK || c->kind == K_BUTTON) {
            MoveWindow(c->ctl, margin, y, w - 2 * margin, px(24), FALSE);
        } else if(c->kind == K_MEMO) {
            MoveWindow(c->label, margin, y + px(3), w - 2 * margin, px(20), FALSE);
            MoveWindow(c->ctl, margin, y + px(24), w - 2 * margin, px(120), FALSE);
            y += px(124);
        } else {
            MoveWindow(c->label, margin, y + px(3), labelW, px(20), FALSE);
            MoveWindow(c->ctl, ctlX, y,
                       ctlW, c->kind == K_COMBO ? px(300) : px(24), FALSE);
        }
        y += rowH;
    }
    gCanvasH = y + margin;

    const int viewH = vr.bottom - vr.top;
    const int maxScroll = gCanvasH > viewH ? gCanvasH - viewH : 0;
    if(gScrollY > maxScroll)
        gScrollY = maxScroll;
    MoveWindow(gCanvas, 0, -gScrollY, w, gCanvasH > viewH ? gCanvasH : viewH,
               TRUE);
    SCROLLINFO si = {sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin  = 0;
    si.nMax  = gCanvasH > 0 ? gCanvasH - 1 : 0;
    si.nPage = viewH;
    si.nPos  = gScrollY;
    SetScrollInfo(gView, SB_VERT, &si, TRUE);
}

void scrollTo(int y)
{
    gScrollY = y < 0 ? 0 : y;
    layoutCanvas();
}

//! Keep the focused control of the pane in view
void followFocus()
{
    HWND f = GetFocus();
    if(f == gLastFocus)
        return;
    gLastFocus = f;
    if(!f || !IsChild(gCanvas, f))
        return;
    while(GetParent(f) != gCanvas) //the edit part of a combo box
        f = GetParent(f);
    //remember it: F6 and returning to this group come back here
    auto known = gByHwnd.find(f);
    if(known != gByHwnd.end())
        gFocusMemo[nodeKey(gPaneNode)] = ctlKey(*known->second);
    RECT r, vr;
    GetWindowRect(f, &r);
    MapWindowPoints(nullptr, gCanvas, (POINT *)&r, 2);
    GetClientRect(gView, &vr);
    const int viewH = vr.bottom;
    if(r.top - px(4) < gScrollY)
        scrollTo(r.top - px(30)); //with the label above it in view
    else if(r.bottom + px(4) > gScrollY + viewH)
        scrollTo(r.bottom + px(8) - viewH);
}

// ------------------------------------------------------ dialogs

struct ValueDlg { Ctl *c; double v; };

INT_PTR CALLBACK valueProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto *d = (ValueDlg *)lp;
            const Param &p = d->c->p;
            SetDlgItemTextW(dlg, IDC_VALUE_LABEL, (p.label + L":").c_str());
            wchar_t b[128];
            swprintf(b, 128, p.type == 'f' ? L"%g" : L"%.0f", d->c->value);
            SetDlgItemTextW(dlg, IDC_VALUE_EDIT, b);
            std::wstring range = L"From " + valueText(p, p.min) + L" (" +
                                 std::to_wstring((long long)std::lround(p.min)) +
                                 L") to " + valueText(p, p.max) + L" (" +
                                 std::to_wstring((long long)std::lround(p.max)) +
                                 L"). Now " + valueText(p, d->c->value) + L".";
            if(p.type == 'f') {
                swprintf(b, 128, L"From %g to %g. Now ", p.min, p.max);
                range = b + valueText(p, d->c->value) + L".";
            }
            SetDlgItemTextW(dlg, IDC_VALUE_RANGE, range.c_str());
            access::setDescription(GetDlgItem(dlg, IDC_VALUE_EDIT), range);
            HWND e = GetDlgItem(dlg, IDC_VALUE_EDIT);
            SetFocus(e);
            SendMessageW(e, EM_SETSEL, 0, -1);
            return FALSE;
        }
        case WM_COMMAND:
            if(LOWORD(wp) == IDOK) {
                auto *d = (ValueDlg *)GetWindowLongPtrW(dlg, DWLP_USER);
                wchar_t b[128] = L"";
                GetDlgItemTextW(dlg, IDC_VALUE_EDIT, b, 128);
                wchar_t *end = nullptr;
                double v = wcstod(b, &end);
                if(end == b) {
                    MessageBoxW(dlg, L"Please type a number.", L"Type a value",
                                MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                const Param &p = d->c->p;
                if(v < p.min) v = p.min;
                if(v > p.max) v = p.max;
                d->v = v;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if(LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

void typeValue(Ctl &c)
{
    ValueDlg d{&c, c.value};
    if(DialogBoxParamW(gInst, MAKEINTRESOURCEW(IDD_VALUE), gMain, valueProc,
                       (LPARAM)&d) == IDOK) {
        sendValue(c, d.v);
        showValue(c);
        SetFocus(c.ctl);
        say(c.p.label + L" " + valueText(c.p, d.v));
    }
}

INT_PTR CALLBACK settingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    static std::vector<std::string> audio, midi;
    switch(msg) {
        case WM_INITDIALOG: {
            audio = zyn::Nio::getAudioDevices();
            midi  = zyn::Nio::getMidiDevices();
            HWND a = GetDlgItem(dlg, IDC_AUDIO_DEVICE);
            HWND m = GetDlgItem(dlg, IDC_MIDI_DEVICE);
            SendMessageW(a, CB_ADDSTRING, 0, (LPARAM)L"Default output");
            int sel = 0;
            for(size_t i = 0; i < audio.size(); ++i) {
                SendMessageW(a, CB_ADDSTRING, 0, (LPARAM)widen(audio[i]).c_str());
                if(audio[i] == zyn::Nio::audioDevice)
                    sel = (int)i + 1;
            }
            SendMessageW(a, CB_SETCURSEL, sel, 0);
            SendMessageW(m, CB_ADDSTRING, 0, (LPARAM)L"Every MIDI input");
            sel = 0;
            for(size_t i = 0; i < midi.size(); ++i) {
                SendMessageW(m, CB_ADDSTRING, 0, (LPARAM)widen(midi[i]).c_str());
                if(midi[i] == zyn::Nio::midiDevice)
                    sel = (int)i + 1;
            }
            SendMessageW(m, CB_SETCURSEL, sel, 0);
            return TRUE;
        }
        case WM_COMMAND:
            if(LOWORD(wp) == IDOK) {
                int a = (int)SendDlgItemMessageW(dlg, IDC_AUDIO_DEVICE,
                                                 CB_GETCURSEL, 0, 0);
                int m = (int)SendDlgItemMessageW(dlg, IDC_MIDI_DEVICE,
                                                 CB_GETCURSEL, 0, 0);
                std::string an = a > 0 ? audio[a - 1] : std::string();
                std::string mn = m > 0 ? midi[m - 1] : std::string();
                if(an != zyn::Nio::audioDevice)
                    send("/io/audio-device", "s", an.c_str());
                if(mn != zyn::Nio::midiDevice)
                    send("/io/midi-device", "s", mn.c_str());
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if(LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

bool fileDialog(bool save, const wchar_t *filter, const wchar_t *ext,
                std::string &out)
{
    wchar_t file[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner   = gMain;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH * 4;
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if(!ok)
        return false;
    out = narrow(file);
    return true;
}

std::wstring fileName(const std::string &path)
{
    std::wstring w = widen(path);
    size_t s = w.find_last_of(L"\\/");
    return s == std::wstring::npos ? w : w.substr(s + 1);
}

// ------------------------------------------------------- bank browser

int bankPart()
{
    if(!gBankDlg)
        return currentPart();
    int i = (int)SendDlgItemMessageW(gBankDlg, IDC_BANK_PART, CB_GETCURSEL, 0, 0);
    return i < 0 ? 0 : i;
}

void requestSlots()
{
    for(auto &s : gSlots)
        s = Slot();
    gSlotsChanged = true;
    for(int i = 0; i < kBankSlots; ++i)
        request("/bank/slot" + std::to_string(i));
}

//! The instrument list again, after slots arrived (once per tick)
void refillInstruments()
{
    if(!gBankDlg || !gSlotsChanged)
        return;
    gSlotsChanged = false;
    HWND l = GetDlgItem(gBankDlg, IDC_INSTR_LIST);
    int keep = -1;
    int sel = (int)SendMessageW(l, LB_GETCURSEL, 0, 0);
    if(sel >= 0)
        keep = (int)SendMessageW(l, LB_GETITEMDATA, sel, 0);
    SendMessageW(l, WM_SETREDRAW, FALSE, 0);
    SendMessageW(l, LB_RESETCONTENT, 0, 0);
    int newSel = -1, count = 0;
    for(int i = 0; i < kBankSlots; ++i) {
        if(gSlots[i].name.empty())
            continue;
        std::wstring t = widen(gSlots[i].name) + L" (slot " +
                         std::to_wstring(i + 1) + L")";
        int at = (int)SendMessageW(l, LB_ADDSTRING, 0, (LPARAM)t.c_str());
        SendMessageW(l, LB_SETITEMDATA, at, i);
        if(i == keep)
            newSel = at;
        ++count;
    }
    SendMessageW(l, LB_SETCURSEL, newSel >= 0 ? newSel : 0, 0);
    SendMessageW(l, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(l, nullptr, TRUE);
}

int selectedSlot()
{
    HWND l = GetDlgItem(gBankDlg, IDC_INSTR_LIST);
    int sel = (int)SendMessageW(l, LB_GETCURSEL, 0, 0);
    return sel < 0 ? -1 : (int)SendMessageW(l, LB_GETITEMDATA, sel, 0);
}

void loadSelectedInstrument()
{
    const int slot = selectedSlot();
    if(slot < 0 || gSlots[slot].file.empty()) {
        say(L"No instrument selected");
        return;
    }
    const int part = bankPart();
    gPreviewSlot = slot;
    gPreviewPart = part;
    gPreviewBank = gBankPos;
    send("/load_xiz", "is", part, gSlots[slot].file.c_str());
    send(("/part" + std::to_string(part) + "/Penabled").c_str(), "T");
    say(L"Loading " + widen(gSlots[slot].name) + L" into part " +
        std::to_wstring(part + 1));
}

INT_PTR CALLBACK bankProc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch(msg) {
        case WM_INITDIALOG: {
            //MiddleWare answers its own ports at once, inside the request:
            //the replies below arrive before CreateDialogParam returns
            gBankDlg = dlg;
            HWND p = GetDlgItem(dlg, IDC_BANK_PART);
            for(int i = 0; i < kParts; ++i)
                SendMessageW(p, CB_ADDSTRING, 0,
                             (LPARAM)(L"Part " + std::to_wstring(i + 1)).c_str());
            SendMessageW(p, CB_SETCURSEL, currentPart(), 0);
            request("/bank/bank_list");
            request("/bank/bank_select");
            return TRUE;
        }
        case WM_COMMAND:
            switch(LOWORD(wp)) {
                case IDC_BANK_LIST:
                    if(HIWORD(wp) == LBN_SELCHANGE) {
                        int sel = (int)SendDlgItemMessageW(dlg, IDC_BANK_LIST,
                                                           LB_GETCURSEL, 0, 0);
                        int i = sel < 0 ? -1 :
                            (int)SendDlgItemMessageW(dlg, IDC_BANK_LIST,
                                                     LB_GETITEMDATA, sel, 0);
                        if(i >= 0) {
                            gBankPos = i;
                            send("/bank/bank_select", "i", i);
                            requestSlots();
                        }
                    }
                    return TRUE;
                case IDC_INSTR_LIST:
                    if(HIWORD(wp) == LBN_DBLCLK)
                        loadSelectedInstrument();
                    return TRUE;
                case IDOK:
                    loadSelectedInstrument();
                    return TRUE;
                case IDCANCEL:
                    DestroyWindow(dlg);
                    return TRUE;
            }
            break;
        case WM_DESTROY:
            if(gPreviewNote >= 0) {
                send("/noteOff", "ii", gPreviewChan, gPreviewNote);
                gPreviewNote = -1;
            }
            gBankDlg = nullptr;
            if(gMain)
                SetFocus(gTree);
            return TRUE;
    }
    return FALSE;
}

void openBanks()
{
    if(gBankDlg) {
        SetFocus(GetDlgItem(gBankDlg, IDC_INSTR_LIST));
        return;
    }
    gBankDlg = CreateDialogParamW(gInst, MAKEINTRESOURCEW(IDD_BANK), gMain,
                                  bankProc, 0);
    ShowWindow(gBankDlg, SW_SHOW);
}

//! Space in the instrument list plays a note while it is held
bool bankKey(const MSG &m)
{
    if(!gBankDlg || GetFocus() != GetDlgItem(gBankDlg, IDC_INSTR_LIST) ||
       m.wParam != VK_SPACE)
        return false;
    if(m.message == WM_KEYDOWN) {
        if(!(m.lParam & (1 << 30)) && gPreviewNote < 0) {
            //the selected instrument, loaded into the part first when it
            //is not there yet; MiddleWare loads it before the note arrives
            const int slot = selectedSlot();
            const int part = bankPart();
            if(slot >= 0 && !gSlots[slot].file.empty() &&
               (gPreviewSlot != slot || gPreviewPart != part ||
                gPreviewBank != gBankPos))
                loadSelectedInstrument();
            gPreviewSlot = slot;
            gPreviewPart = part;
            gPreviewBank = gBankPos;
            gPreviewChan = part % 16;
            gPreviewNote = 60;
            send("/noteOn", "iii", gPreviewChan, gPreviewNote, 100);
        }
        return true;
    }
    if(m.message == WM_KEYUP) {
        if(gPreviewNote >= 0)
            send("/noteOff", "ii", gPreviewChan, gPreviewNote);
        gPreviewNote = -1;
        return true;
    }
    return m.message == WM_CHAR;
}

// ------------------------------------------- playing from the keyboard

//! Semitone above the octave's C for a key, or -1. Lower row from Z,
//! upper row from Q, as on a piano keyboard (the black keys a row higher).
//! Keys are told apart by their position (scan code), not by the letter the
//! layout prints on them, so the rows are the same on every layout. The
//! names below are those of a US keyboard.
int keyOffset(LPARAM lp)
{
    if(lp & (1 << 24))
        return -1; //an extended key (the numeric keypad's, arrows ...)
    static const std::pair<int, int> keys[] = {
        {0x2C, 0},  /* Z */ {0x1F, 1},  /* S */ {0x2D, 2},  /* X */
        {0x20, 3},  /* D */ {0x2E, 4},  /* C */ {0x2F, 5},  /* V */
        {0x22, 6},  /* G */ {0x30, 7},  /* B */ {0x23, 8},  /* H */
        {0x31, 9},  /* N */ {0x24, 10}, /* J */ {0x32, 11}, /* M */
        {0x33, 12}, /* , */ {0x26, 13}, /* L */ {0x34, 14}, /* . */
        {0x27, 15}, /* ; */ {0x35, 16}, /* / */
        {0x10, 12}, /* Q */ {0x03, 13}, /* 2 */ {0x11, 14}, /* W */
        {0x04, 15}, /* 3 */ {0x12, 16}, /* E */ {0x13, 17}, /* R */
        {0x06, 18}, /* 5 */ {0x14, 19}, /* T */ {0x07, 20}, /* 6 */
        {0x15, 21}, /* Y */ {0x08, 22}, /* 7 */ {0x16, 23}, /* U */
        {0x17, 24}, /* I */ {0x0A, 25}, /* 9 */ {0x18, 26}, /* O */
        {0x0B, 27}, /* 0 */ {0x19, 28}, /* P */
    };
    const int scan = (int)((lp >> 16) & 0xFF);
    for(auto &k : keys)
        if(k.first == scan)
            return k.second;
    return -1;
}

void playTitle()
{
    std::wstring t = L"Play: part " + std::to_wstring(currentPart() + 1) +
                     L", octave " + std::to_wstring(gPlayOctave) +
                     L", velocity " + std::to_wstring(gPlayVelocity);
    SetWindowTextW(gPlayWnd, t.c_str());
}

void releaseAll()
{
    for(auto &h : gHeld)
        send("/noteOff", "ii", h.second.first, h.second.second);
    gHeld.clear();
}

LRESULT CALLBACK playProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
        case WM_KEYDOWN: {
            if(wp == VK_ESCAPE) {
                DestroyWindow(h);
                return 0;
            }
            if(wp == VK_PRIOR || wp == VK_NEXT) {
                releaseAll();
                gPlayOctave += wp == VK_PRIOR ? 1 : -1;
                gPlayOctave = gPlayOctave < 0 ? 0 : (gPlayOctave > 8 ? 8 : gPlayOctave);
                playTitle();
                say(L"Octave " + std::to_wstring(gPlayOctave));
                return 0;
            }
            if(wp == VK_UP || wp == VK_DOWN) {
                gPlayVelocity += wp == VK_UP ? 10 : -10;
                gPlayVelocity = gPlayVelocity < 1 ? 1 : (gPlayVelocity > 127 ? 127 : gPlayVelocity);
                playTitle();
                say(L"Velocity " + std::to_wstring(gPlayVelocity));
                return 0;
            }
            const int off = keyOffset(lp);
            const WPARAM scan = (lp >> 16) & 0xFF;
            if(off < 0 || (lp & (1 << 30)) || gHeld.count(scan))
                return 0;
            const int note = 12 * (gPlayOctave + 1) + off;
            if(note > 127)
                return 0;
            const int chan = currentPart() % 16;
            gHeld[scan] = {chan, note};
            send("/noteOn", "iii", chan, note, gPlayVelocity);
            return 0;
        }
        case WM_KEYUP: {
            auto it = gHeld.find((lp >> 16) & 0xFF);
            if(it != gHeld.end()) {
                send("/noteOff", "ii", it->second.first, it->second.second);
                gHeld.erase(it);
            }
            return 0;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
            return 0; //no beeps
        case WM_KILLFOCUS:
            releaseAll();
            return 0;
        case WM_TIMER:
            if(access::onTimer(wp))
                return 0;
            break;
        case WM_DESTROY:
            releaseAll();
            gPlayWnd = nullptr;
            if(gMain)
                SetFocus(gTree);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void openPlay()
{
    if(gPlayWnd) {
        SetFocus(gPlayWnd);
        return;
    }
    gPlayWnd = CreateWindowExW(0, L"ZynAccessiblePlay", L"Play",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, px(420), px(120),
                               gMain, nullptr, gInst, nullptr);
    playTitle();
    ShowWindow(gPlayWnd, SW_SHOW);
    SetFocus(gPlayWnd);
    say(L"Keys Z to slash and Q to P play notes. Page Up and Page Down "
        L"change octave, Up and Down velocity. Escape closes.");
}

// ------------------------------------------------------ the held note

//! MIDI note number as a name: 60 is C4
std::wstring noteNameOf(int n)
{
    static const wchar_t *names[12] = {L"C", L"C sharp", L"D", L"D sharp",
        L"E", L"F", L"F sharp", L"G", L"G sharp", L"A", L"A sharp", L"B"};
    if(n < 0 || n > 127)
        return std::to_wstring(n);
    return std::wstring(names[n % 12]) + std::to_wstring(n / 12 - 1);
}

//! The interface's own settings: %APPDATA%\ZynAddSubFX\accessible-ui.ini
std::wstring uiSettingsFile()
{
    wchar_t buf[MAX_PATH * 2] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH * 2);
    std::wstring dir = n ? std::wstring(buf) + L"\\ZynAddSubFX" : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\accessible-ui.ini";
}

void loadUiSettings()
{
    const std::wstring f = uiSettingsFile();
    gHoldNote = GetPrivateProfileIntW(L"HeldNote", L"Note", 48, f.c_str());
    gHoldVelocity = GetPrivateProfileIntW(L"HeldNote", L"Velocity", 100,
                                          f.c_str());
    if(gHoldNote < 0 || gHoldNote > 127)
        gHoldNote = 48;
    if(gHoldVelocity < 1 || gHoldVelocity > 127)
        gHoldVelocity = 100;
}

void saveUiSettings()
{
    const std::wstring f = uiSettingsFile();
    WritePrivateProfileStringW(L"HeldNote", L"Note",
                               std::to_wstring(gHoldNote).c_str(), f.c_str());
    WritePrivateProfileStringW(L"HeldNote", L"Velocity",
                               std::to_wstring(gHoldVelocity).c_str(),
                               f.c_str());
}

void holdNoteOff()
{
    if(!gHoldOn)
        return;
    send("/noteOff", "ii", gHoldChan, gHoldPlaying);
    gHoldOn = false;
}

//! F5: the held note on, or off again
void toggleHoldNote()
{
    if(gHoldOn) {
        holdNoteOff();
        say(L"Held note off");
        return;
    }
    gHoldChan    = currentPart() % 16;
    gHoldPlaying = gHoldNote;
    send("/noteOn", "iii", gHoldChan, gHoldPlaying, gHoldVelocity);
    gHoldOn = true;
    say(L"Holding " + noteNameOf(gHoldPlaying) + L" on part " +
        std::to_wstring(currentPart() + 1) + L". F5 stops it");
}

INT_PTR CALLBACK holdNoteProc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch(msg) {
        case WM_INITDIALOG: {
            HWND n = GetDlgItem(dlg, IDC_HOLD_NOTE);
            for(int i = 0; i < 128; ++i)
                SendMessageW(n, CB_ADDSTRING, 0, (LPARAM)(noteNameOf(i) +
                             L" (" + std::to_wstring(i) + L")").c_str());
            SendMessageW(n, CB_SETCURSEL, gHoldNote, 0);
            HWND v = GetDlgItem(dlg, IDC_HOLD_VELOCITY);
            for(int i = 1; i <= 127; ++i)
                SendMessageW(v, CB_ADDSTRING, 0,
                             (LPARAM)std::to_wstring(i).c_str());
            SendMessageW(v, CB_SETCURSEL, gHoldVelocity - 1, 0);
            return TRUE;
        }
        case WM_COMMAND:
            if(LOWORD(wp) == IDOK) {
                int n = (int)SendDlgItemMessageW(dlg, IDC_HOLD_NOTE,
                                                 CB_GETCURSEL, 0, 0);
                int v = (int)SendDlgItemMessageW(dlg, IDC_HOLD_VELOCITY,
                                                 CB_GETCURSEL, 0, 0);
                if(n >= 0)
                    gHoldNote = n;
                if(v >= 0)
                    gHoldVelocity = v + 1;
                saveUiSettings();
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if(LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// ------------------------------------------------- part names in tree

void updatePartItem(int part)
{
    if(part < 0 || part >= kParts || !gPartItem[part])
        return;
    std::wstring t = L"Part " + std::to_wstring(part + 1);
    if(!gPartName[part].empty())
        t += L": " + gPartName[part];
    if(!gPartEnabled[part])
        t += L", off";
    TVITEMW it = {};
    it.mask    = TVIF_TEXT;
    it.hItem   = gPartItem[part];
    it.pszText = (LPWSTR)t.c_str();
    SendMessageW(gTree, TVM_SETITEMW, 0, (LPARAM)&it);
}

//! Ask for the pane's values again in a moment: after a command whose
//! effect reaches the engine later (a scale loaded into the tuning)
constexpr UINT_PTR kRefreshTimer = 0x5A2;
void refreshPaneSoon()
{
    if(gMain)
        SetTimer(gMain, kRefreshTimer, 400, nullptr);
}

void requestPartNames()
{
    for(int i = 0; i < kParts; ++i) {
        request("/part" + std::to_string(i) + "/Pname");
        request("/part" + std::to_string(i) + "/Penabled");
    }
}

//! Replies the tree and the bank browser want (whatever the pane shows)
void handleSideMessage(const char *msg, const char *args)
{
    if(!gEngineSwitch.empty() && gEngineSwitch == msg &&
       (args[0] == 'T' || args[0] == 'F')) {
        gEngineOff = args[0] == 'F';
        if(gHeading)
            SetWindowTextW(gHeading, gEngineOff
                               ? (gHeadingText + L" " + engineOffNote()).c_str()
                               : gHeadingText.c_str());
    }
    int part = -1;
    char field[32] = "";
    if(sscanf(msg, "/part%d/%31s", &part, field) == 2 && part >= 0 &&
       part < kParts && gPartItem[part]) {
        if(!strcmp(field, "Pname") && args[0] == 's') {
            gPartName[part] = widen(rtosc_argument(msg, 0).s);
            updatePartItem(part);
            return;
        }
        if(!strcmp(field, "Penabled") && (args[0] == 'T' || args[0] == 'F')) {
            gPartEnabled[part] = args[0] == 'T';
            updatePartItem(part);
            return;
        }
    }
    {
        const std::string path = msg;
        const size_t slash = path.rfind('/');
        if(slash != std::string::npos && path.compare(slash, 9, "/efftype") == 0 &&
           path.size() == slash + 8 && (args[0] == 'i' || args[0] == 'c')) {
            const std::string node = path.substr(0, slash + 1);
            if(isEffectNode(node)) {
                const int type = rtosc_argument(msg, 0).i;
                auto known = gEffType.find(node);
                //only a new or changed type: the pane's own request for
                //efftype must not rebuild the pane, which requests it again
                if(known == gEffType.end() || known->second != type) {
                    gEffType[node] = type;
                    refreshEffect(node);
                    if(gPaneNode.path == node) {
                        auto item = gItemByPath.find(node);
                        buildPane(gPaneNode, item != gItemByPath.end()
                                                 ? nodeTitle(item->second)
                                                 : nodeLabel(gPaneNode));
                    }
                }
            }
            //no return: the pane may show this value too
        }
    }
    if(!strcmp(msg, "/bank/bank_list")) {
        gBanks.clear();
        const int n = (int)strlen(args);
        for(int i = 0; i + 1 < n; i += 2)
            gBanks.emplace_back(rtosc_argument(msg, i).s,
                                rtosc_argument(msg, i + 1).s);
        if(gBankDlg) {
            HWND l = GetDlgItem(gBankDlg, IDC_BANK_LIST);
            SendMessageW(l, LB_RESETCONTENT, 0, 0);
            for(size_t i = 0; i < gBanks.size(); ++i) {
                int at = (int)SendMessageW(l, LB_ADDSTRING, 0,
                                           (LPARAM)widen(gBanks[i].first).c_str());
                SendMessageW(l, LB_SETITEMDATA, at, (LPARAM)i);
            }
            if(gBanks.empty())
                say(L"No instrument banks found");
            if(gBankPos >= 0)
                SendMessageW(l, LB_SETCURSEL, gBankPos, 0);
        }
        return;
    }
    if(!strcmp(msg, "/bank/bank_select") && !strcmp(args, "i")) {
        gBankPos = rtosc_argument(msg, 0).i;
        if(gBankDlg) {
            SendDlgItemMessageW(gBankDlg, IDC_BANK_LIST, LB_SETCURSEL, gBankPos, 0);
            requestSlots();
        }
        return;
    }
    if(!strcmp(msg, "/bankview") && !strcmp(args, "iss")) {
        int i = rtosc_argument(msg, 0).i;
        if(i >= 0 && i < kBankSlots) {
            gSlots[i].name = rtosc_argument(msg, 1).s;
            gSlots[i].file = rtosc_argument(msg, 2).s;
            gSlotsChanged  = true;
        }
        return;
    }
}

// ------------------------------------------------------- navigation

void fillChildren(HTREEITEM item);
std::vector<Node> shownChildren(const Node &n);

const Node &itemNode(HTREEITEM item)
{
    TVITEMW it = {};
    it.mask  = TVIF_PARAM;
    it.hItem = item;
    TreeView_GetItem(gTree, &it);
    return gNodes[(size_t)it.lParam];
}

//! The tree item of a group (opening the groups above it as needed), or null
HTREEITEM findItem(const std::string &path, const std::wstring &section)
{
    HTREEITEM item = TreeView_GetRoot(gTree);
    while(item) {
        const Node &n = itemNode(item);
        if(n.path == path && n.section == section)
            return item;
        fillChildren(item);
        HTREEITEM next = nullptr;
        for(HTREEITEM c = TreeView_GetChild(gTree, item); c;
            c = TreeView_GetNextSibling(gTree, c)) {
            const Node &cn = itemNode(c);
            if(cn.path == path && cn.section == section)
                return c;
            //a group above the one wanted, or the group whose section is
            //wanted: look inside
            if(!next && cn.section.empty() &&
               path.compare(0, cn.path.size(), cn.path) == 0 &&
               (cn.path.size() < path.size() || !section.empty()))
                next = c;
        }
        item = next;
    }
    return nullptr;
}

//! Choose a group in the tree; with @p focusKey, focus that setting in its
//! pane (or the remembered one, or the first)
void goToGroup(HTREEITEM item, bool intoPane, const std::string &focusKey)
{
    TreeView_SelectItem(gTree, item); //builds its pane
    TreeView_EnsureVisible(gTree, item);
    if(!intoPane)
        return;
    Ctl *c = focusKey.empty() ? nullptr : findCtl(focusKey);
    if(c)
        SetFocus(c->ctl);
    else if(!focusPane())
        SetFocus(gTree);
    access::announce(nodeTitle(item));
}

//! The same group of the previous or next voice, part, kit item, effect
//! slot ...: the numbered level deepest in the current group's path changes
std::string siblingPath(const std::string &path, int dir)
{
    std::vector<std::string> segs;
    size_t start = 1;
    while(start < path.size()) {
        size_t e = path.find('/', start);
        if(e == std::string::npos)
            e = path.size();
        segs.push_back(path.substr(start, e - start));
        start = e + 1;
    }
    for(int i = (int)segs.size() - 1; i >= 0; --i) {
        std::string &s = segs[i];
        size_t d = s.size();
        while(d > 0 && isdigit((unsigned char)s[d - 1]))
            --d;
        if(d == s.size() || d == 0)
            continue;
        const int n = atoi(s.c_str() + d) + dir;
        if(n < 0)
            return std::string();
        s = s.substr(0, d) + std::to_string(n);
        std::string out = "/";
        for(auto &x : segs)
            out += x + "/";
        return out;
    }
    return std::string();
}

void siblingGroup(int dir)
{
    const std::string target = siblingPath(gPaneNode.path, dir);
    HTREEITEM item = target.empty() ? nullptr
                                    : findItem(target, gPaneNode.section);
    if(!item) {
        say(dir > 0 ? L"No next group of this kind"
                    : L"No previous group of this kind");
        return;
    }
    //the setting focused here, focused again there
    HWND f = GetFocus();
    if(f && !gByHwnd.count(f) && GetParent(f))
        f = GetParent(f);
    auto c = gByHwnd.find(f);
    const bool inPane = c != gByHwnd.end();
    const std::string key = inPane ? ctlKey(*c->second) : std::string();
    goToGroup(item, inPane, key);
}

//! Ctrl+Down, Ctrl+Up in a pane with section headings
bool jumpSection(int dir)
{
    int at = -1, n = (int)gCtls.size();
    HWND f = GetFocus();
    if(f && !gByHwnd.count(f) && GetParent(f))
        f = GetParent(f);
    for(int i = 0; i < n; ++i)
        if(gCtls[i]->ctl == f)
            at = i;
    std::vector<int> heads;
    for(int i = 0; i < n; ++i)
        if(gCtls[i]->kind == K_HEADING)
            heads.push_back(i);
    if(heads.empty() || at < 0)
        return false;
    int cur = -1; //the section the focus is in
    for(size_t h = 0; h < heads.size(); ++h)
        if(heads[h] < at)
            cur = (int)h;
    int target;
    if(dir > 0)
        target = cur + 1;
    else //to the start of this section, or of the previous one from its start
        target = (cur >= 0 && at > heads[cur] + 1) ? cur : cur - 1;
    if(target < 0 || target >= (int)heads.size()) {
        say(dir > 0 ? L"Last section" : L"First section");
        return true;
    }
    for(int i = heads[target] + 1; i < n; ++i)
        if(gCtls[i]->kind != K_HEADING) {
            SetFocus(gCtls[i]->ctl);
            access::announce(L"Section " + gCtls[heads[target]]->p.label);
            return true;
        }
    return true;
}

// -------------------------------------------------- find a setting

struct FindEntry {
    Node         node;   //the group (or section) holding the setting
    std::string  key;    //the setting's key in its pane (ctlKey)
    std::wstring text;   //"Fine detune, in Part 1, Kit item 1, ..."
    std::wstring lower;  //for matching
};
std::vector<FindEntry> gFindIndex;
std::vector<int>       gFindShown;

std::wstring lowered(std::wstring s)
{
    for(auto &c : s)
        c = (wchar_t)towlower(c);
    return s;
}

//! Every setting below @p n, in the current part's current kit item only
//! (the other parts and kit items have the same settings)
void collectSettings(const Node &n, const std::wstring &where, int part,
                     int kit, int depth)
{
    if(depth > 14)
        return;
    auto add = [&](const Node &holder, const std::wstring &place,
                   const Param &p) {
        FindEntry e;
        e.node = holder;
        const std::string path = p.path;
        e.key  = path.compare(0, holder.path.size(), holder.path) == 0
                     ? path.substr(holder.path.size()) : path;
        if(p.count > 0)
            e.key = p.path.substr(holder.path.size()); //the array itself
        e.text  = p.label + L", in " + place;
        e.lower = lowered(e.text + L" " + p.doc);
        gFindIndex.push_back(e);
    };
    std::vector<Section> secs = isEffectNode(n.path) ? std::vector<Section>()
                                                      : sections(n);
    if(!secs.empty()) {
        for(const Section &s : secs) {
            Node holder    = n;
            holder.section = s.label;
            for(const Param &p : s.params)
                add(holder, where + L", " + s.label, p);
        }
    } else
        for(const Param &p : params(n))
            add(n, where, p);

    for(const Node &c : shownChildren(n)) {
        if(!c.section.empty())
            continue; //sections were covered above
        int cp = -1, ck = -1;
        char rest[16] = "";
        if(sscanf(c.path.c_str(), "/part%d/%15s", &cp, rest) >= 1 && cp != part)
            continue;
        if(sscanf(c.path.c_str(), "/part%*d/kit%d/", &ck) == 1 && ck != kit)
            continue;
        collectSettings(c, depth == 0 ? c.label : where + L", " + c.label,
                        part, kit, depth + 1);
    }
}

void refillFind(HWND dlg)
{
    wchar_t buf[256] = L"";
    GetDlgItemTextW(dlg, IDC_FIND_EDIT, buf, 256);
    std::vector<std::wstring> words;
    std::wstring w, all = lowered(buf) + L" ";
    for(wchar_t c : all) {
        if(c == L' ') {
            if(!w.empty())
                words.push_back(w);
            w.clear();
        } else
            w += c;
    }
    HWND l = GetDlgItem(dlg, IDC_FIND_LIST);
    SendMessageW(l, WM_SETREDRAW, FALSE, 0);
    SendMessageW(l, LB_RESETCONTENT, 0, 0);
    gFindShown.clear();
    constexpr size_t kMax = 300;
    size_t found = 0;
    if(!words.empty())
        for(size_t i = 0; i < gFindIndex.size(); ++i) {
            bool ok = true;
            for(auto &x : words)
                if(gFindIndex[i].lower.find(x) == std::wstring::npos)
                    ok = false;
            if(!ok)
                continue;
            ++found;
            if(gFindShown.size() < kMax) {
                gFindShown.push_back((int)i);
                SendMessageW(l, LB_ADDSTRING, 0,
                             (LPARAM)gFindIndex[i].text.c_str());
            }
        }
    SendMessageW(l, LB_SETCURSEL, 0, 0);
    SendMessageW(l, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(l, nullptr, TRUE);
    std::wstring count = words.empty() ? L"Type to search."
        : found == 0 ? L"Nothing found."
        : std::to_wstring(found) + (found == 1 ? L" setting found" : L" settings found") +
          (found > kMax ? L", the first " + std::to_wstring(kMax) + L" shown." : L".");
    SetDlgItemTextW(dlg, IDC_FIND_COUNT, count.c_str());
}

INT_PTR CALLBACK findProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            refillFind(dlg);
            SetFocus(GetDlgItem(dlg, IDC_FIND_EDIT));
            return FALSE;
        case WM_COMMAND:
            if(LOWORD(wp) == IDC_FIND_EDIT && HIWORD(wp) == EN_CHANGE) {
                refillFind(dlg);
                return TRUE;
            }
            if(LOWORD(wp) == IDOK ||
               (LOWORD(wp) == IDC_FIND_LIST && HIWORD(wp) == LBN_DBLCLK)) {
                int sel = (int)SendDlgItemMessageW(dlg, IDC_FIND_LIST,
                                                   LB_GETCURSEL, 0, 0);
                if(sel < 0 || sel >= (int)gFindShown.size()) {
                    MessageBoxW(dlg, L"Type part of a setting's name, then "
                                L"choose it in the list.", L"Find a setting",
                                MB_OK | MB_ICONINFORMATION);
                    return TRUE;
                }
                *(int *)GetWindowLongPtrW(dlg, DWLP_USER) = gFindShown[sel];
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if(LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

void findSetting()
{
    //settings of the master, the effects, and the current part's current
    //kit item
    const int part = currentPart();
    int kit = 0;
    sscanf(gPaneNode.path.c_str(), "/part%*d/kit%d/", &kit);
    gFindIndex.clear();
    collectSettings(rootNode(), L"Master", part, kit, 0);

    int chosen = -1;
    if(DialogBoxParamW(gInst, MAKEINTRESOURCEW(IDD_FIND), gMain, findProc,
                       (LPARAM)&chosen) != IDOK || chosen < 0)
        return;
    const FindEntry e = gFindIndex[chosen];
    HTREEITEM item = findItem(e.node.path, e.node.section);
    if(!item) {
        say(L"That group is not in the tree now");
        return;
    }
    goToGroup(item, true, e.key);
}

const wchar_t *kHelp =
    L"Keys\n\n"
    L"F6: move between the tree and the settings of the selected group "
    L"(to the setting last used there).\n"
    L"Alt+Left, Alt+Right: the same group of the previous or next voice, "
    L"part, kit item or effect, keeping the setting.\n"
    L"Ctrl+Down, Ctrl+Up (in the settings): next or previous section.\n"
    L"Ctrl+F: find a setting by name.\n"
    L"Tree: arrows move, Right opens a group, Left closes it. Typing a name "
    L"jumps to it.\n"
    L"Settings: Tab and Shift+Tab move between controls.\n"
    L"Sliders: arrows change by one step, Page Up and Page Down by ten, Home "
    L"and End go to the ends. Enter types a value. Backspace resets to the "
    L"default.\n"
    L"Lists and check boxes: Backspace resets to the default.\n\n"
    L"Ctrl+N: new (reset everything)\n"
    L"Ctrl+O: open a master file (.xmz)\n"
    L"Ctrl+S: save a master file\n"
    L"Ctrl+B: instrument banks (Enter loads, Space plays a note)\n"
    L"Ctrl+K: play from the computer keyboard\n"
    L"Ctrl+R: record to a WAV file (from the next note), or stop recording\n"
    L"F5: hold a note on the current part while you edit; F5 again stops it "
    L"(Options, Held note and velocity, chooses the note)\n"
    L"Ctrl+I: open an instrument (.xiz) into the current part\n"
    L"Ctrl+Shift+S: save the current part as an instrument\n"
    L"Ctrl+Z, Ctrl+Y: undo, redo\n"
    L"Ctrl+Comma: audio and MIDI settings\n"
    L"F1: this list\n\n"
    L"Everything is described in MANUAL.txt, next to the program.";

void command(int id)
{
    std::string f;
    switch(id) {
        case IDM_NEW:
            if(MessageBoxW(gMain, L"Reset every part and setting?", L"New",
                           MB_YESNO | MB_ICONQUESTION) == IDYES) {
                send("/reset_master", "");
                say(L"Reset");
            }
            break;
        case IDM_OPEN:
            if(fileDialog(false, L"Master files (*.xmz)\0*.xmz\0All files\0*.*\0",
                          L"xmz", f)) {
                send("/load_xmz", "s", f.c_str());
                say(L"Opening " + fileName(f));
            }
            break;
        case IDM_SAVE:
            if(fileDialog(true, L"Master files (*.xmz)\0*.xmz\0", L"xmz", f)) {
                send("/save_xmz", "s", f.c_str());
                say(L"Saved " + fileName(f));
            }
            break;
        case IDM_OPEN_INSTR:
            if(fileDialog(false,
                          L"Instruments (*.xiz)\0*.xiz\0All files\0*.*\0",
                          L"xiz", f)) {
                send("/load_xiz", "is", currentPart(), f.c_str());
                say(L"Opening " + fileName(f) + L" into part " +
                    std::to_wstring(currentPart() + 1));
            }
            break;
        case IDM_SAVE_INSTR:
            if(fileDialog(true, L"Instruments (*.xiz)\0*.xiz\0", L"xiz", f)) {
                send("/save_xiz", "is", currentPart(), f.c_str());
                say(L"Saved part " + std::to_wstring(currentPart() + 1) +
                    L" as " + fileName(f));
            }
            break;
        case IDM_OPEN_SCL:
            if(fileDialog(false, L"Scala scales (*.scl)\0*.scl\0All files\0*.*\0",
                          L"scl", f)) {
                send("/load_scl", "s", f.c_str());
                refreshPaneSoon();
                say(L"Opened the scale " + fileName(f) +
                    L". Switch Tuning on to use it");
            }
            break;
        case IDM_OPEN_KBM:
            if(fileDialog(false,
                          L"Scala keyboard mappings (*.kbm)\0*.kbm\0All files\0*.*\0",
                          L"kbm", f)) {
                send("/load_kbm", "s", f.c_str());
                refreshPaneSoon();
                say(L"Opened the keyboard mapping " + fileName(f));
            }
            break;
        case IDM_OPEN_XSZ:
            if(fileDialog(false,
                          L"ZynAddSubFX scales (*.xsz)\0*.xsz\0All files\0*.*\0",
                          L"xsz", f)) {
                send("/load_xsz", "s", f.c_str());
                refreshPaneSoon();
                say(L"Opened the scale " + fileName(f));
            }
            break;
        case IDM_SAVE_XSZ:
            if(fileDialog(true, L"ZynAddSubFX scales (*.xsz)\0*.xsz\0", L"xsz",
                          f)) {
                send("/save_xsz", "s", f.c_str());
                say(L"Saved the scale as " + fileName(f));
            }
            break;
        case IDM_EXIT:
            DestroyWindow(gMain);
            break;
        case IDM_UNDO:
            send("/undo", "");
            say(L"Undo");
            break;
        case IDM_REDO:
            send("/redo", "");
            say(L"Redo");
            break;
        case IDM_SETTINGS:
            DialogBoxParamW(gInst, MAKEINTRESOURCEW(IDD_SETTINGS), gMain,
                            settingsProc, 0);
            break;
        case IDM_HELP_KEYS:
            MessageBoxW(gMain, kHelp, L"Keys", MB_OK);
            break;
        case IDM_ABOUT: {
            std::wstring t = L"ZynAddSubFX Accessible Edition\n\n"
                L"ZynAddSubFX version " +
                std::to_wstring(zyn::version.get_major()) + L"." +
                std::to_wstring(zyn::version.get_minor()) + L"." +
                std::to_wstring(zyn::version.get_revision()) +
                L"\nCopyright (C) 2002-2013 Nasca Octavian Paul and others\n"
                L"Copyright (C) 2009-2026 Mark McCurry and the ZynAddSubFX "
                L"contributors\n\n"
                L"Accessible Windows interface: dengopaiv, with Claude.\n\n"
                L"Free software under the GNU General Public License, "
                L"version 2 or later.";
            MessageBoxW(gMain, t.c_str(), L"About", MB_OK);
            break;
        }
        case IDM_SWITCH_PANE: {
            HWND f = GetFocus();
            if(f == gTree || !IsChild(gCanvas, f)) {
                if(focusPane())
                    access::announce(nodeTitle(TreeView_GetSelection(gTree)) +
                                     (gEngineOff ? L". " + engineOffNote()
                                                 : std::wstring()));
                else
                    say(L"No settings here");
            } else
                SetFocus(gTree);
            break;
        }
        case IDM_FOCUS_TREE:
            SetFocus(gTree);
            break;
        case IDM_PREV_GROUP:
        case IDM_NEXT_GROUP:
            siblingGroup(id == IDM_NEXT_GROUP ? 1 : -1);
            break;
        case IDM_FIND:
            findSetting();
            break;
        case IDM_HOLD_NOTE:
            toggleHoldNote();
            break;
        case IDM_RECORD:
            if(gRecording) {
                send("/HDDRecorder/stop", "");
                gRecording = false;
                say(L"Recording stopped. Saved " + gRecordFile);
            } else if(fileDialog(true, L"Wave files (*.wav)\0*.wav\0", L"wav",
                                 f)) {
                send("/HDDRecorder/preparefile", "s", f.c_str());
                send("/HDDRecorder/start", "");
                gRecording  = true;
                gRecordFile = fileName(f);
                say(L"Ready to record into " + gRecordFile + L". Recording "
                    L"starts with the next note. Ctrl+R stops it");
            }
            break;
        case IDM_HOLD_NOTE_SETTINGS:
            if(DialogBoxParamW(gInst, MAKEINTRESOURCEW(IDD_HOLDNOTE), gMain,
                               holdNoteProc, 0) == IDOK)
                say(L"Held note: " + noteNameOf(gHoldNote) + L", velocity " +
                    std::to_wstring(gHoldVelocity) +
                    (gHoldOn ? L". It applies from the next F5" : L""));
            break;
        case IDM_NEXT_SECTION:
        case IDM_PREV_SECTION:
            if(!jumpSection(id == IDM_NEXT_SECTION ? 1 : -1))
                say(L"No sections here");
            break;
        case IDM_BANK:
            openBanks();
            break;
        case IDM_PLAY:
            openPlay();
            break;
    }
}

HMENU makeMenu()
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(),
          opts = CreatePopupMenu(), help = CreatePopupMenu(),
          go = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_NEW, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save as...\tCtrl+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_BANK, L"Instrument &banks...\tCtrl+B");
    AppendMenuW(file, MF_STRING, IDM_OPEN_INSTR,
                L"Open &instrument into current part...\tCtrl+I");
    AppendMenuW(file, MF_STRING, IDM_SAVE_INSTR,
                L"Save current part as instr&ument...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_RECORD,
                L"&Record to WAV file, or stop recording...\tCtrl+R");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_OPEN_SCL, L"Open Scala s&cale (.scl)...");
    AppendMenuW(file, MF_STRING, IDM_OPEN_KBM,
                L"Open Scala &keyboard mapping (.kbm)...");
    AppendMenuW(file, MF_STRING, IDM_OPEN_XSZ, L"Open sca&le (.xsz)...");
    AppendMenuW(file, MF_STRING, IDM_SAVE_XSZ, L"Save scale as (.xs&z)...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit\tAlt+F4");
    AppendMenuW(edit, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, IDM_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_FIND, L"&Find a setting...\tCtrl+F");
    AppendMenuW(go, MF_STRING, IDM_SWITCH_PANE,
                L"&Tree or settings\tF6");
    AppendMenuW(go, MF_STRING, IDM_PREV_GROUP,
                L"&Previous group of the same kind\tAlt+Left");
    AppendMenuW(go, MF_STRING, IDM_NEXT_GROUP,
                L"&Next group of the same kind\tAlt+Right");
    //Ctrl+Down and Ctrl+Up are keys of the settings themselves (handleKey),
    //so they are only shown here, not accelerators
    AppendMenuW(go, MF_STRING, IDM_NEXT_SECTION,
                L"Ne&xt section of the settings\tCtrl+Down");
    AppendMenuW(go, MF_STRING, IDM_PREV_SECTION,
                L"Pre&vious section of the settings\tCtrl+Up");
    AppendMenuW(opts, MF_STRING, IDM_HOLD_NOTE,
                L"&Hold a note on or off\tF5");
    AppendMenuW(opts, MF_STRING, IDM_HOLD_NOTE_SETTINGS,
                L"Held &note and velocity...");
    AppendMenuW(opts, MF_STRING, IDM_PLAY,
                L"&Play from computer keyboard...\tCtrl+K");
    AppendMenuW(opts, MF_STRING, IDM_SETTINGS, L"&Audio and MIDI...\tCtrl+Comma");
    AppendMenuW(help, MF_STRING, IDM_HELP_KEYS, L"&Keys\tF1");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)go, L"&Go");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)opts, L"&Options");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

HACCEL makeAccel()
{
    ACCEL a[] = {
        {FCONTROL | FVIRTKEY, 'N', IDM_NEW},
        {FCONTROL | FVIRTKEY, 'O', IDM_OPEN},
        {FCONTROL | FVIRTKEY, 'S', IDM_SAVE},
        {FCONTROL | FVIRTKEY, 'I', IDM_OPEN_INSTR},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', IDM_SAVE_INSTR},
        {FCONTROL | FVIRTKEY, 'Z', IDM_UNDO},
        {FCONTROL | FVIRTKEY, 'Y', IDM_REDO},
        {FCONTROL | FVIRTKEY, VK_OEM_COMMA, IDM_SETTINGS},
        {FCONTROL | FVIRTKEY, 'B', IDM_BANK},
        {FCONTROL | FVIRTKEY, 'K', IDM_PLAY},
        {FVIRTKEY, VK_F1, IDM_HELP_KEYS},
        {FVIRTKEY, VK_F6, IDM_SWITCH_PANE},
        {FSHIFT | FVIRTKEY, VK_F6, IDM_SWITCH_PANE},
        {FALT | FVIRTKEY, VK_LEFT, IDM_PREV_GROUP},
        {FALT | FVIRTKEY, VK_RIGHT, IDM_NEXT_GROUP},
        {FCONTROL | FVIRTKEY, 'F', IDM_FIND},
        {FVIRTKEY, VK_F5, IDM_HOLD_NOTE},
        {FCONTROL | FVIRTKEY, 'R', IDM_RECORD},
    };
    return CreateAcceleratorTableW(a, sizeof(a) / sizeof(a[0]));
}

// ------------------------------------------------------------- the tree

//! The groups of a node the tree shows: an effect slot shows only the
//! settings of its current effect type
std::vector<Node> shownChildren(const Node &n)
{
    std::vector<Node> all = children(n);
    if(!isEffectNode(n.path))
        return all;
    auto t = gEffType.find(n.path);
    if(t == gEffType.end())
        return all; //not known yet: everything, until the type arrives
    std::vector<Node> out;
    for(const Node &c : all) {
        std::string base = c.path.substr(n.path.size());
        if(!base.empty() && base.back() == '/')
            base.pop_back();
        if(base == effectGroup(t->second) ||
           (base == "filterpars" && t->second == kDynFilterType))
            out.push_back(c);
    }
    return out;
}

HTREEITEM insertNode(HTREEITEM parent, const Node &n)
{
    gNodes.push_back(n);
    const std::wstring label = nodeLabel(n);
    TVINSERTSTRUCTW ins = {};
    ins.hParent      = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask    = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    ins.item.pszText = (LPWSTR)label.c_str();
    ins.item.lParam  = (LPARAM)(gNodes.size() - 1);
    ins.item.cChildren = shownChildren(n).empty() ? 0 : 1;
    HTREEITEM item =
        (HTREEITEM)SendMessageW(gTree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    gItemByPath[nodeKey(n)] = item;
    int part = -1;
    char rest[8] = "";
    //the part's own item, not one of its sections (they share its path)
    if(n.section.empty() &&
       sscanf(n.path.c_str(), "/part%d%7s", &part, rest) == 2 &&
       !strcmp(rest, "/") && part >= 0 && part < kParts)
        gPartItem[part] = item;
    if(isEffectNode(n.path) && !gEffType.count(n.path))
        request(n.path + "efftype");
    return item;
}

void fillChildren(HTREEITEM item)
{
    if(TreeView_GetChild(gTree, item))
        return;
    TVITEMW it = {};
    it.mask  = TVIF_PARAM;
    it.hItem = item;
    TreeView_GetItem(gTree, &it);
    const Node n = gNodes[(size_t)it.lParam];
    for(const Node &c : shownChildren(n))
        insertNode(item, c);
}

//! An effect slot's type arrived or changed: its name and its groups
void refreshEffect(const std::string &path)
{
    auto f = gItemByPath.find(path);
    if(f == gItemByPath.end())
        return;
    HTREEITEM item = f->second;
    TVITEMW it = {};
    it.mask  = TVIF_PARAM;
    it.hItem = item;
    if(!TreeView_GetItem(gTree, &it))
        return;
    const Node n = gNodes[(size_t)it.lParam];
    const std::wstring label = nodeLabel(n);
    const bool hasKids = !shownChildren(n).empty();
    it.mask      = TVIF_TEXT | TVIF_CHILDREN;
    it.pszText   = (LPWSTR)label.c_str();
    it.cChildren = hasKids ? 1 : 0;
    SendMessageW(gTree, TVM_SETITEMW, 0, (LPARAM)&it);
    if(TreeView_GetChild(gTree, item)) { //already open: its groups again
        const bool open = TreeView_GetItemState(gTree, item, TVIS_EXPANDED) &
                          TVIS_EXPANDED;
        HTREEITEM sel = TreeView_GetSelection(gTree);
        bool selInside = false;
        for(HTREEITEM p = sel; p; p = TreeView_GetParent(gTree, p))
            if(p == item && p != sel)
                selInside = true;
        if(selInside)
            TreeView_SelectItem(gTree, item);
        for(auto i = gItemByPath.begin(); i != gItemByPath.end();) {
            if(i->first.size() > path.size() &&
               i->first.compare(0, path.size(), path) == 0)
                i = gItemByPath.erase(i);
            else
                ++i;
        }
        while(HTREEITEM c = TreeView_GetChild(gTree, item))
            TreeView_DeleteItem(gTree, c);
        fillChildren(item);
        if(open && hasKids)
            TreeView_Expand(gTree, item, TVE_EXPAND);
    }
}

LRESULT onTreeNotify(NMHDR *h)
{
    switch(h->code) {
        case TVN_ITEMEXPANDINGW: {
            auto *tv = (NMTREEVIEWW *)h;
            if(tv->action & TVE_EXPAND)
                fillChildren(tv->itemNew.hItem);
            return 0;
        }
        case TVN_SELCHANGEDW: {
            auto *tv = (NMTREEVIEWW *)h;
            const Node n = gNodes[(size_t)tv->itemNew.lParam];
            buildPane(n, nodeTitle(tv->itemNew.hItem));
            return 0;
        }
    }
    return 0;
}

// ---------------------------------------------------------- window procs

LRESULT CALLBACK canvasProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
        case WM_HSCROLL: {
            auto it = gByHwnd.find((HWND)lp);
            if(it == gByHwnd.end())
                break;
            Ctl &c = *it->second;
            const int pos = (int)SendMessageW(c.ctl, TBM_GETPOS, 0, 0);
            if(LOWORD(wp) == TB_ENDTRACK)
                return 0;
            if(c.kind == K_INDEX) {
                if(pos != (int)c.value)
                    chooseElement(c, pos);
                return 0;
            }
            if(c.known && pos == valueToPos(c.p, c.value))
                return 0; //nothing moved
            sendValue(c, posToValue(c.p, pos));
            return 0;
        }
        case WM_COMMAND: {
            auto it = gByHwnd.find((HWND)lp);
            if(it == gByHwnd.end())
                break;
            Ctl &c = *it->second;
            if(c.kind == K_COMBO && HIWORD(wp) == CBN_SELCHANGE) {
                int i = (int)SendMessageW(c.ctl, CB_GETCURSEL, 0, 0);
                if(i >= 0)
                    sendValue(c, (double)SendMessageW(c.ctl, CB_GETITEMDATA, i, 0));
            } else if(c.kind == K_CHECK && HIWORD(wp) == BN_CLICKED) {
                sendValue(c, SendMessageW(c.ctl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            } else if((c.kind == K_EDIT || c.kind == K_MEMO) &&
                      HIWORD(wp) == EN_KILLFOCUS) {
                sendText(c);
            } else if(c.kind == K_BUTTON && HIWORD(wp) == BN_CLICKED) {
                runAction(c);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK viewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
        case WM_VSCROLL: {
            SCROLLINFO si = {sizeof(si)};
            si.fMask = SIF_ALL;
            GetScrollInfo(h, SB_VERT, &si);
            int y = gScrollY;
            switch(LOWORD(wp)) {
                case SB_LINEUP:        y -= px(26); break;
                case SB_LINEDOWN:      y += px(26); break;
                case SB_PAGEUP:        y -= (int)si.nPage; break;
                case SB_PAGEDOWN:      y += (int)si.nPage; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: y = si.nTrackPos; break;
                case SB_TOP:           y = 0; break;
                case SB_BOTTOM:        y = gCanvasH; break;
            }
            scrollTo(y);
            return 0;
        }
        case WM_MOUSEWHEEL:
            scrollTo(gScrollY - GET_WHEEL_DELTA_WPARAM(wp) * px(26) * 3 / WHEEL_DELTA);
            return 0;
        case WM_SIZE:
            layoutCanvas();
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void makeFont()
{
    if(gFont)
        DeleteObject(gFont);
    NONCLIENTMETRICSW ncm = {sizeof(ncm)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0,
                               gDpi);
    gFont = CreateFontIndirectW(&ncm.lfMessageFont);
}

void layoutMain()
{
    RECT r;
    GetClientRect(gMain, &r);
    SendMessageW(gStatus, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(gStatus, &sr);
    const int statusH = sr.bottom - sr.top;
    const int h = r.bottom - statusH;
    const int treeW = r.right * 35 / 100;
    const int labelH = px(20);
    MoveWindow(gTreeLabel, px(4), px(2), treeW - px(4), labelH, TRUE);
    MoveWindow(gTree, 0, labelH + px(4), treeW, h - labelH - px(4), TRUE);
    MoveWindow(gView, treeW + px(4), 0, r.right - treeW - px(4), h, TRUE);
}

LRESULT CALLBACK mainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
        case WM_COMMAND:
            if(lp == 0) { //menu or accelerator
                command(LOWORD(wp));
                return 0;
            }
            break;
        case WM_NOTIFY:
            if(((NMHDR *)lp)->hwndFrom == gTree)
                return onTreeNotify((NMHDR *)lp);
            break;
        case WM_SIZE:
            if(gTree)
                layoutMain();
            return 0;
        case WM_ACTIVATE:
            if(LOWORD(wp) == WA_INACTIVE)
                gSavedFocus = GetFocus();
            else {
                SetFocus(gSavedFocus && IsWindow(gSavedFocus) ? gSavedFocus
                                                              : gTree);
                return 0;
            }
            break;
        case WM_TIMER:
            if(access::onTimer(wp))
                return 0;
            if(wp == kRefreshTimer) { //refreshPaneSoon
                KillTimer(h, kRefreshTimer);
                for(auto &c : gCtls)
                    if(c->kind != K_BUTTON && c->kind != K_INDEX &&
                       c->kind != K_HEADING)
                        request(c->p.path);
                return 0;
            }
            break;
        case WM_DPICHANGED: {
            gDpi = HIWORD(wp);
            RECT *r = (RECT *)lp;
            makeFont();
            for(HWND c : {gTreeLabel, gTree, gStatus})
                SendMessageW(c, WM_SETFONT, (WPARAM)gFont, TRUE);
            for(HWND c = GetWindow(gCanvas, GW_CHILD); c;
                c = GetWindow(c, GW_HWNDNEXT))
                SendMessageW(c, WM_SETFONT, (WPARAM)gFont, TRUE);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left,
                         r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            holdNoteOff();
            if(gRecording) //finish the file
                send("/HDDRecorder/stop", "");
            clearPane();
            access::shutdown();
            gMain = gTree = gView = gCanvas = gStatus = gTreeLabel = nullptr;
            if(gExit)
                *gExit = 1;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// -------------------------------------------------- keys before dialogs

bool handleKey(MSG &m)
{
    if(!gMain)
        return false;
    if(m.message != WM_KEYDOWN)
        return false;
    HWND f = GetFocus();
    auto it = gByHwnd.find(f);
    if(it == gByHwnd.end()) {
        //the edit box inside a combo box
        if(f && GetParent(f) && gByHwnd.count(GetParent(f)))
            it = gByHwnd.find(GetParent(f));
        else
            return false;
    }
    Ctl &c = *it->second;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    if(c.kind == K_MEMO) {
        //several lines of text: Enter is a new line, Ctrl+Enter applies,
        //Tab still moves on
        if(m.wParam == VK_RETURN && ctrl) {
            sendText(c);
            say(L"Applied");
            return true;
        }
        if(m.wParam == VK_TAB && !ctrl && !alt) {
            HWND next = GetNextDlgTabItem(gMain, c.ctl, shift);
            if(next)
                SetFocus(next);
            return true;
        }
        return false;
    }
    if(ctrl && !alt && (m.wParam == VK_DOWN || m.wParam == VK_UP))
        return jumpSection(m.wParam == VK_DOWN ? 1 : -1);
    if(ctrl || alt)
        return false;
    if(c.kind == K_BUTTON || c.kind == K_INDEX)
        return false;
    if(m.wParam == VK_RETURN) {
        if(c.kind == K_SLIDER) {
            typeValue(c);
            return true;
        }
        if(c.kind == K_EDIT) {
            sendText(c);
            say(L"Set");
            return true;
        }
    }
    if(m.wParam == VK_BACK && c.kind != K_EDIT) {
        bool ok;
        double v = defaultOf(c.p, &ok);
        if(ok) {
            sendValue(c, v);
            showValue(c);
            say(L"Default, " + valueText(c.p, v));
        } else
            say(L"No default known");
        return true;
    }
    return false;
}

bool registerClasses()
{
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc   = mainProc;
    wc.hInstance     = gInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ZynAccessibleMain";
    wc.hIcon         = LoadIconW(gInst, MAKEINTRESOURCEW(IDI_ZYN));
    wc.hIconSm       = wc.hIcon;
    if(!RegisterClassExW(&wc))
        return false;
    wc.lpfnWndProc   = viewProc;
    wc.lpszClassName = L"ZynAccessibleView";
    wc.hIcon = wc.hIconSm = nullptr;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if(!RegisterClassExW(&wc))
        return false;
    wc.lpfnWndProc   = canvasProc;
    wc.lpszClassName = L"ZynAccessibleCanvas";
    if(!RegisterClassExW(&wc))
        return false;
    wc.lpfnWndProc   = playProc;
    wc.lpszClassName = L"ZynAccessiblePlay";
    return RegisterClassExW(&wc) != 0;
}

void restoreAutosave(int id)
{
    if(MessageBoxW(gMain,
                   L"ZynAddSubFX did not close properly last time. Restore "
                   L"the settings it saved automatically?",
                   L"Restore", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        send("/reload_auto_save", "i", id);
        say(L"Restored");
    } else
        send("/delete_auto_save", "i", id);
}

void handleMessage(const char *msg)
{
    if(!msg || !*msg)
        return;
    trace("<", msg);
    const char *args = rtosc_argument_string(msg);
    handleSideMessage(msg, args);
    auto it = gByPath.find(msg);
    if(it != gByPath.end()) {
        Ctl &c = *it->second;
        if(!*args)
            return;
        rtosc_arg_t a = rtosc_argument(msg, 0);
        switch(args[0]) {
            case 'i': c.value = a.i; break;
            case 'c': c.value = a.i; break;
            case 'f': c.value = a.f; break;
            case 'd': c.value = a.d; break;
            case 'T': c.value = 1; break;
            case 'F': c.value = 0; break;
            case 's':
                if((c.kind == K_EDIT || c.kind == K_MEMO) &&
                   GetFocus() != c.ctl) {
                    std::wstring t = widen(a.s), shown;
                    for(wchar_t ch : t) { //an edit box breaks lines at \r\n
                        if(ch == L'\n' && c.kind == K_MEMO)
                            shown += L'\r';
                        shown += ch;
                    }
                    SetWindowTextW(c.ctl, shown.c_str());
                }
                return;
            default: return;
        }
        c.known = true;
        showValue(c);
        return;
    }
    if(!strcmp(msg, "/damage") && args[0] == 's') {
        const std::string prefix = rtosc_argument(msg, 0).s;
        const std::string &pane = gPaneNode.path;
        if(pane.compare(0, prefix.size(), prefix) == 0 ||
           prefix.compare(0, pane.size(), pane) == 0)
            for(auto &c : gCtls)
                request(c->p.path);
        if(prefix == "/" || prefix.compare(0, 5, "/part") == 0)
            requestPartNames();
        return;
    }
    if(!strcmp(msg, "/show")) {
        if(gMain) {
            ShowWindow(gMain, SW_SHOW);
            SetForegroundWindow(gMain);
        }
        return;
    }
    if(!strcmp(msg, "/alert") && args[0] == 's') {
        say(widen(rtosc_argument(msg, 0).s));
        return;
    }
    if(!strcmp(msg, "/alert-reload") && args[0] == 'i') {
        restoreAutosave(rtosc_argument(msg, 0).i);
        return;
    }
}

} //namespace

// ------------------------------------------------- the GUI:: interface

namespace GUI {

ui_handle_t createUi(Fl_Osc_Interface *, void *exit)
{
    gInst = GetModuleHandleW(nullptr);
    gExit = (int *)exit;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX icc = {sizeof(icc),
                                ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES |
                                ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    if(!registerClasses())
        return nullptr;

    gMain = CreateWindowExW(WS_EX_CONTROLPARENT, L"ZynAccessibleMain",
                            L"ZynAddSubFX Accessible Edition", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, nullptr,
                            makeMenu(), gInst, nullptr);
    if(!gMain)
        return nullptr;
    gDpi = GetDpiForWindow(gMain);
    SetWindowPos(gMain, nullptr, 0, 0, px(1000), px(700),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    makeFont();
    access::init(gMain);
    gAccel = makeAccel();

    gTreeLabel = CreateWindowExW(0, L"STATIC", L"Structure:",
                                 WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
                                 gMain, nullptr, gInst, nullptr);
    gTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"Structure",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS |
                            TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                            0, 0, 10, 10, gMain, nullptr, gInst, nullptr);
    gView = CreateWindowExW(WS_EX_CONTROLPARENT, L"ZynAccessibleView",
                            L"Settings", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                            WS_CLIPCHILDREN, 0, 0, 10, 10, gMain, nullptr,
                            gInst, nullptr);
    gCanvas = CreateWindowExW(WS_EX_CONTROLPARENT, L"ZynAccessibleCanvas",
                              L"Settings", WS_CHILD | WS_VISIBLE |
                              WS_CLIPCHILDREN, 0, 0, 10, 10, gView, nullptr,
                              gInst, nullptr);
    gStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                              WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                              gMain, nullptr, gInst, nullptr);
    for(HWND c : {gTreeLabel, gTree, gStatus})
        SendMessageW(c, WM_SETFONT, (WPARAM)gFont, FALSE);
    access::setName(gTree, L"Structure");

    HTREEITEM root = insertNode(TVI_ROOT, rootNode());
    fillChildren(root);
    TreeView_Expand(gTree, root, TVE_EXPAND);
    layoutMain();
    TreeView_SelectItem(gTree, root);
    SetFocus(gTree);
    requestPartNames();
    loadUiSettings();
    status(L"Ready. F1 lists the keys.");
    return (ui_handle_t)gMain;
}

void destroyUi(ui_handle_t)
{
    if(gMain)
        DestroyWindow(gMain);
    if(gAccel) {
        DestroyAcceleratorTable(gAccel);
        gAccel = nullptr;
    }
    if(gFont) {
        DeleteObject(gFont);
        gFont = nullptr;
    }
}

void raiseUi(ui_handle_t, const char *msg)
{
    handleMessage(msg);
}

void raiseUi(ui_handle_t gui, const char *path, const char *args, ...)
{
    char buffer[2048];
    va_list va;
    va_start(va, args);
    if(rtosc_vmessage(buffer, sizeof(buffer), path, args, va))
        raiseUi(gui, buffer);
    va_end(va);
}

void tickUi(ui_handle_t)
{
    MSG m;
    while(PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if(m.message == WM_QUIT) {
            if(gExit)
                *gExit = 1;
            continue;
        }
        if(bankKey(m))
            continue;
        //the tree's numeric-keypad asterisk opens every group below: on
        //Master or a part that is millions of groups
        if(m.message == WM_KEYDOWN && m.hwnd == gTree &&
           m.wParam == VK_MULTIPLY) {
            say(L"Opening every group at once is not available: there are "
                L"too many. Open them one at a time with Right Arrow.");
            continue;
        }
        if(gMain && gAccel && TranslateAcceleratorW(gMain, gAccel, &m))
            continue;
        if(handleKey(m))
            continue;
        if(gBankDlg && IsDialogMessageW(gBankDlg, &m))
            continue;
        if(gMain && IsDialogMessageW(gMain, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if(gMain)
        followFocus();
    refillInstruments();
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
}

Fl_Osc_Interface *genOscInterface(zyn::MiddleWare *mw)
{
    gMw = mw;
    return new Fl_Osc_Interface();
}

}
