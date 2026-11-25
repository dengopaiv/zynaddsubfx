/*
  ZynAddSubFX - a software synthesizer

  PortModel.cpp - What the accessible UI shows, read from the engine's ports

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#include "PortModel.h"
#include "Overlay.h"

#include <rtosc/ports.h>
#include "../../Misc/MiddleWare.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace zynui {

std::wstring widen(const std::string &s)
{
    if(s.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring &w)
{
    if(w.empty())
        return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                                nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n,
                        nullptr, nullptr);
    return s;
}

//! "part#16/" -> base "part", count 16; "adpars/" -> "adpars", 0
static void splitName(const char *portName, std::string &base, int &count,
                      std::string &args)
{
    std::string n = portName;
    args.clear();
    size_t colon = n.find(':');
    if(colon != std::string::npos) {
        args = n.substr(colon);
        n    = n.substr(0, colon);
    }
    if(!n.empty() && n.back() == '/')
        n.pop_back();
    count = 0;
    size_t hash = n.find('#');
    if(hash != std::string::npos) {
        count = atoi(n.c_str() + hash + 1);
        n     = n.substr(0, hash);
    }
    base = n;
}

static bool isParameter(const rtosc::Port &p)
{
    auto meta = p.meta();
    return meta.find("parameter") != meta.end() &&
           meta.find("internal") == meta.end();
}

//! Whether a group holds any parameter, at any depth (memoised)
static bool hasContent(const rtosc::Ports *ports)
{
    static std::map<const rtosc::Ports *, bool> memo;
    static std::set<const rtosc::Ports *>       visiting;
    if(!ports)
        return false;
    auto it = memo.find(ports);
    if(it != memo.end())
        return it->second;
    if(visiting.count(ports))
        return false; //recursive structure: decided by the outer call
    visiting.insert(ports);
    bool found = false;
    for(const rtosc::Port &p : ports->ports) {
        if(p.ports) {
            std::string base, args;
            int count;
            splitName(p.name, base, count, args);
            if(!overlay::hiddenGroup(base) && hasContent(p.ports)) {
                found = true;
                break;
            }
        } else if(isParameter(p)) {
            found = true;
            break;
        }
    }
    visiting.erase(ports);
    memo[ports] = found;
    return found;
}

Node rootNode()
{
    Node n;
    n.path  = "/";
    n.ports = &zyn::MiddleWare::getAllPorts();
    n.label = L"Master";
    return n;
}

std::vector<Node> children(const Node &n)
{
    std::vector<Node> out;
    if(!n.ports)
        return out;
    std::set<std::string> seen;
    for(const rtosc::Port &p : n.ports->ports) {
        if(!p.ports)
            continue;
        std::string base, args;
        int count;
        splitName(p.name, base, count, args);
        if(base.empty() || overlay::hiddenGroup(base) || !hasContent(p.ports))
            continue;
        if(!seen.insert(base).second)
            continue; //merged port trees may list a group twice
        const char *doc = p.meta()["documentation"];
        if(count > 0) {
            for(int i = 0; i < count; ++i) {
                Node c;
                c.path  = n.path + base + std::to_string(i) + "/";
                c.ports = p.ports;
                c.label = overlay::groupLabel(base, doc) + L" " +
                          std::to_wstring(i + 1);
                out.push_back(c);
            }
        } else {
            Node c;
            c.path  = n.path + base + "/";
            c.ports = p.ports;
            c.label = overlay::groupLabel(base, doc);
            out.push_back(c);
        }
    }
    return out;
}

static double number(const char *s, double fallback)
{
    if(!s || !*s)
        return fallback;
    char *end = nullptr;
    double v  = strtod(s, &end);
    return end == s ? fallback : v;
}

std::vector<Param> params(const Node &n)
{
    std::vector<Param> out;
    if(!n.ports)
        return out;
    std::set<std::string> seen;
    for(const rtosc::Port &p : n.ports->ports) {
        if(p.ports || !isParameter(p))
            continue;
        std::string base, args;
        int count;
        splitName(p.name, base, count, args);
        if(count > 0 || base.empty())
            continue; //arrays (harmonics, envelope points) get editors of their own
        if(!seen.insert(base).second)
            continue;

        auto  meta = p.meta();
        Param q;
        q.name = base;
        q.path = n.path + base;

        const char *a = args.c_str();
        if(strchr(a, 'f'))      q.type = 'f';
        else if(strchr(a, 'i')) q.type = 'i';
        else if(strchr(a, 'c')) q.type = 'c';
        else if(strchr(a, 'T')) q.type = 'T';
        else if(strchr(a, 's')) q.type = 's';
        else                    continue;
        q.send = q.type;

        const char *doc   = meta["documentation"];
        const char *sname = meta["shortname"];
        q.doc   = doc ? widen(doc) : std::wstring();
        q.label = overlay::paramLabel(n.path, base, doc ? doc : "",
                                      sname ? sname : "");

        const char *mn = meta["min"], *mx = meta["max"];
        if(mn && mx) {
            q.hasRange = true;
            q.min = number(mn, 0);
            q.max = number(mx, 127);
        } else if(q.type == 'f') {
            q.min = 0;
            q.max = 1;
        }
        const char *scale = meta["scale"];
        q.logScale = scale && !strcmp(scale, "logarithmic");
        q.logMin   = number(meta["logmin"], 0);
        if(const char *u = meta["unit"])
            q.unit = u;
        if(const char *d = meta["default"])
            q.defaultValue = d;

        for(auto m : meta) {
            if(m.title && !strncmp(m.title, "map ", 4) && m.value)
                q.options.emplace_back(atoi(m.title + 4), widen(m.value));
        }
        if(!q.options.empty() && !q.hasRange) {
            q.min = q.options.front().first;
            q.max = q.options.back().first;
        }
        out.push_back(q);
    }
    overlay::arrange(n.path, out);
    return out;
}

int partOfPath(const std::string &path)
{
    if(path.compare(0, 5, "/part") != 0)
        return -1;
    const char *s = path.c_str() + 5;
    if(*s < '0' || *s > '9')
        return -1;
    return atoi(s);
}

static std::wstring fmt(const wchar_t *f, double v)
{
    wchar_t b[64];
    swprintf(b, 64, f, v);
    return b;
}

std::wstring valueText(const Param &p, double v)
{
    std::wstring special = overlay::formatValue(p, v);
    if(!special.empty())
        return special;

    if(!p.options.empty()) {
        const int id = (int)std::lround(v);
        for(auto &o : p.options)
            if(o.first == id)
                return o.second;
    }
    if(p.type == 'T')
        return v != 0 ? L"on" : L"off";

    const std::string &u = p.unit;
    if(u == "S" || u == "s") {
        if(std::fabs(v) < 1.0)
            return fmt(L"%.0f ms", v * 1000.0);
        return fmt(L"%.2f s", v);
    }
    if(u == "HZ" || u == "Hz") {
        if(std::fabs(v) >= 1000.0)
            return fmt(L"%.2f kHz", v / 1000.0);
        return fmt(v < 10 ? L"%.2f Hz" : L"%.1f Hz", v);
    }
    if(u == "dB")
        return fmt(L"%.1f dB", v);
    if(u == "%" || u == "percent")
        return fmt(L"%.0f %%", v);
    if(u == "1/100 s")
        return fmt(L"%.0f ms", v * 10.0);
    if(u == "% of semitone")
        return fmt(L"%.0f %% of a semitone", v);
    if(u == "semitones")
        return fmt(L"%.2f semitones", v);
    if(u == "BPM")
        return fmt(L"%.1f BPM", v);

    if(p.type == 'f')
        return fmt(std::fabs(v) < 10 ? L"%.3f" : L"%.1f", v);
    return std::to_wstring((long long)std::lround(v));
}

}
