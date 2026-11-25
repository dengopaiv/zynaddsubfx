/*
  ZynAddSubFX - a software synthesizer

  Overlay.h - What the port metadata does not say: friendly names, the
              groups to hide, order, and how values read in units

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#pragma once

#include <string>
#include <vector>

namespace zynui {
struct Param;
struct Node;

namespace overlay {

//! Groups of ports that are not shown (internal, or handled elsewhere)
bool hiddenGroup(const std::string &base);

//! Name of a group in the tree, e.g. "adpars" -> "AddSynth"
std::wstring groupLabel(const std::string &base, const char *doc);

//! Name of a parameter's control
std::wstring paramLabel(const std::string &nodePath, const std::string &base,
                        const std::string &doc, const std::string &shortName);

//! Order and hide the parameters of a pane
void arrange(const std::string &nodePath, std::vector<Param> &params);

//! A value in its unit where the engine's own conversion is known; "" if not
std::wstring formatValue(const Param &p, double v);

//! What an array's elements are called ("Harmonic", "Point", "Key")
std::wstring arrayIndexName(const std::string &base);

//! Controls the metadata does not mark as parameters: actions (buttons)
//! and the tuning texts
void addExtras(const Node &n, std::vector<Param> &params);

}
}
