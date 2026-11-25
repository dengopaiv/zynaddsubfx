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

#include <cmath>
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

enum Kind { K_SLIDER, K_COMBO, K_CHECK, K_EDIT };

struct Ctl {
    Param  p;
    Kind   kind  = K_SLIDER;
    HWND   label = nullptr, ctl = nullptr;
    double value = 0;     //last value known (engine units)
    bool   known = false; //a reply has arrived
    int    lastSentPos = -1;
};
std::vector<std::unique_ptr<Ctl>> gCtls;
std::map<std::string, Ctl *>      gByPath;
std::map<HWND, Ctl *>             gByHwnd;

constexpr int kFirstCtlId = 1000;

enum {
    IDM_NEW = 40001, IDM_OPEN, IDM_SAVE, IDM_OPEN_INSTR, IDM_SAVE_INSTR,
    IDM_EXIT, IDM_UNDO, IDM_REDO, IDM_SETTINGS, IDM_HELP_KEYS, IDM_ABOUT,
    IDM_SWITCH_PANE, IDM_FOCUS_TREE,
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
        case K_EDIT:
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
    wchar_t buf[1024] = L"";
    GetWindowTextW(c.ctl, buf, 1024);
    send(c.p.path.c_str(), "s", narrow(buf).c_str());
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
        TreeView_GetItem(gTree, &it);
        t     = t.empty() ? std::wstring(buf) : std::wstring(buf) + L", " + t;
        item  = TreeView_GetParent(gTree, item);
    }
    return t;
}

void buildPane(const Node &n, const std::wstring &title)
{
    SendMessageW(gCanvas, WM_SETREDRAW, FALSE, 0);
    clearPane();
    gPaneNode = n;

    std::vector<Param> ps = params(n);
    std::wstring head = title;
    if(ps.empty())
        head += L": no settings of its own. Open it in the tree to see its parts.";
    makeChild(L"STATIC", head.c_str(), SS_LEFT | SS_NOPREFIX, -1);

    int id = kFirstCtlId;
    for(Param &p : ps) {
        auto c = std::make_unique<Ctl>();
        c->p   = p;
        if(!p.options.empty())
            c->kind = K_COMBO;
        else if(p.type == 'T')
            c->kind = K_CHECK;
        else if(p.type == 's')
            c->kind = K_EDIT;
        else
            c->kind = K_SLIDER;

        if(c->kind != K_CHECK)
            c->label = makeChild(L"STATIC", (p.label + L":").c_str(),
                                 SS_LEFT | SS_NOPREFIX, -1);
        switch(c->kind) {
            case K_SLIDER:
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
                                   WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                   id);
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
            case K_EDIT:
                c->ctl = makeChild(WC_EDITW, L"",
                                   WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, id);
                break;
        }
        access::setName(c->ctl, p.label);
        access::setDescription(c->ctl, p.doc);
        gByPath[p.path]   = c.get();
        gByHwnd[c->ctl]   = c.get();
        gCtls.push_back(std::move(c));
        ++id;
    }

    gScrollY = 0;
    layoutCanvas();
    SendMessageW(gCanvas, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gCanvas, nullptr, TRUE);

    for(auto &c : gCtls)
        request(c->p.path);
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
        if(c->kind == K_CHECK) {
            MoveWindow(c->ctl, margin, y, w - 2 * margin, px(22), FALSE);
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

const wchar_t *kHelp =
    L"Keys\n\n"
    L"F6: move between the tree and the settings of the selected group.\n"
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
    L"Ctrl+I: open an instrument (.xiz) into the current part\n"
    L"Ctrl+Shift+S: save the current part as an instrument\n"
    L"Ctrl+Z, Ctrl+Y: undo, redo\n"
    L"Ctrl+Comma: audio and MIDI settings\n"
    L"F1: this list";

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
                if(!gCtls.empty()) {
                    SetFocus(gCtls.front()->ctl);
                    access::announce(nodeTitle(TreeView_GetSelection(gTree)));
                } else
                    say(L"No settings here");
            } else
                SetFocus(gTree);
            break;
        }
        case IDM_FOCUS_TREE:
            SetFocus(gTree);
            break;
    }
}

HMENU makeMenu()
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(),
          opts = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_NEW, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save as...\tCtrl+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_OPEN_INSTR,
                L"Open &instrument into current part...\tCtrl+I");
    AppendMenuW(file, MF_STRING, IDM_SAVE_INSTR,
                L"Save current part as instr&ument...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit\tAlt+F4");
    AppendMenuW(edit, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, IDM_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(opts, MF_STRING, IDM_SETTINGS, L"&Audio and MIDI...\tCtrl+Comma");
    AppendMenuW(help, MF_STRING, IDM_HELP_KEYS, L"&Keys\tF1");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
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
        {FVIRTKEY, VK_F1, IDM_HELP_KEYS},
        {FVIRTKEY, VK_F6, IDM_SWITCH_PANE},
        {FSHIFT | FVIRTKEY, VK_F6, IDM_SWITCH_PANE},
    };
    return CreateAcceleratorTableW(a, sizeof(a) / sizeof(a[0]));
}

// ------------------------------------------------------------- the tree

HTREEITEM insertNode(HTREEITEM parent, const Node &n)
{
    gNodes.push_back(n);
    TVINSERTSTRUCTW ins = {};
    ins.hParent      = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask    = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    ins.item.pszText = (LPWSTR)gNodes.back().label.c_str();
    ins.item.lParam  = (LPARAM)(gNodes.size() - 1);
    ins.item.cChildren = children(n).empty() ? 0 : 1;
    return (HTREEITEM)SendMessageW(gTree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
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
    for(const Node &c : children(n))
        insertNode(item, c);
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
            } else if(c.kind == K_EDIT && HIWORD(wp) == EN_KILLFOCUS) {
                sendText(c);
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
    if(ctrl || alt)
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
                if(c.kind == K_EDIT && GetFocus() != c.ctl)
                    SetWindowTextW(c.ctl, widen(a.s).c_str());
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
                            L"ZynAddSubFX", WS_OVERLAPPEDWINDOW,
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
        if(gMain && gAccel && TranslateAcceleratorW(gMain, gAccel, &m))
            continue;
        if(handleKey(m))
            continue;
        if(gMain && IsDialogMessageW(gMain, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if(gMain)
        followFocus();
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
}

Fl_Osc_Interface *genOscInterface(zyn::MiddleWare *mw)
{
    gMw = mw;
    return new Fl_Osc_Interface();
}

}
