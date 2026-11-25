/*
  ZynAddSubFX - a software synthesizer

  PortModel.h - What the accessible UI shows, read from the engine's ports

  Every parameter of the engine is an OSC port that describes itself: a
  name, a description, a range, a default, choices, sometimes a unit. The
  tree of the window and the controls of each pane are built from that
  metadata, so a parameter added to the engine appears here by itself.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rtosc { struct Ports; struct Port; }

namespace zynui {

//! One parameter: one control in a pane
struct Param {
    std::string  path;     //!< full OSC path, e.g. "/part0/Pvolume"
    std::string  name;     //!< the port's own name, e.g. "Pvolume"
    std::wstring label;    //!< what the control is called
    std::wstring doc;      //!< the engine's description (accessible description)
    char         type = 0; //!< 'i' integer, 'c' char, 'f' float, 'T' toggle, 's' text
    char         send = 0; //!< argument type to send: 'i', 'c', 'f', 'T', 's'
    double       min = 0, max = 127;
    bool         hasRange = false;
    bool         logScale = false;
    double       logMin = 0;
    std::string  unit;     //!< as the engine gives it ("Hz", "dB", "S", ...)
    std::string  defaultValue; //!< as the engine gives it, may be empty
    std::vector<std::pair<int, std::wstring>> options; //!< choices
    bool         action = false; //!< takes no value: a button
    int          actionArg = -1; //!< an action's integer argument, -1 none
    int          count = 0;      //!< an array (harmonics, points): its size;
                                 //!< path is then the element path without
                                 //!< its number
    std::wstring indexName;      //!< an array's element: "Harmonic", "Point"
    bool         multiline = false; //!< text of several lines
};

//! One node of the tree: a group of parameters and further groups
struct Node {
    std::string        path;   //!< "/part0/kit0/adpars/", root "/"
    const rtosc::Ports *ports = nullptr;
    std::wstring       label;  //!< "Part 1", "AddSynth", "Voice 3" ...
    //! A section of a large group ("Punch" of PADsynth): the same path and
    //! ports, showing only that section's settings. Empty for the whole group.
    std::wstring       section;
};

//! A named part of a large group's settings
struct Section {
    std::wstring       label;
    std::vector<Param> params;
};

//! The sections of @p n's settings, in order; empty when the group is small
//! enough to show whole (or is itself a section)
std::vector<Section> sections(const Node &n);

//! Every setting of @p n, even when it is a section: the whole group's
std::vector<Param> allParams(const Node &n);

//! The root: everything the engine and MiddleWare expose
Node rootNode();

//! The groups below @p n that hold at least one parameter, in port order
std::vector<Node> children(const Node &n);

//! The parameters directly in @p n
std::vector<Param> params(const Node &n);

//! The named choices of port @p port inside group @p group of @p n
//! (e.g. the presets of an effect slot's current effect type); empty if none
std::vector<std::pair<int, std::wstring>>
groupPortOptions(const Node &n, const std::string &group, const std::string &port);

//! The part a path belongs to (0 for "/part0/..."), or -1
int partOfPath(const std::string &path);

//! The text a value is spoken and shown as
std::wstring valueText(const Param &p, double v);

//! UTF-8 to UTF-16 and back
std::wstring widen(const std::string &s);
std::string  narrow(const std::wstring &s);

}
