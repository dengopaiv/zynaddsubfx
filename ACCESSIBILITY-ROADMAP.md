# ZynAddSubFX, accessible edition: roadmap

Written 25 November 2026. The aim: ZynAddSubFX, at upstream's latest
version, builds on Windows with Visual Studio, has no decorative graphics,
and every part of it can be used with NVDA, JAWS and Narrator through
standard Windows controls.

## 0. Status (25 November 2026, evening)

Phases 1 to 9 are done on the branch `accessible` of
`dengopaiv/zynaddsubfx`, which is now that fork's default branch. It starts
from upstream `master` at `c379439e`, the latest version, and the fork's
`master` was fast-forwarded to it. Decisions D1 to D5 were taken as
recommended below and confirmed: Win32, the existing fork with an
`accessible` branch, MSVC, standalone first, and the name "ZynAddSubFX
Accessible Edition". Also confirmed: no upstream pull requests for now, and
no separate Linux testing.

What exists now:

* **Builds with Visual Studio 2026 and vcpkg.** No warnings, with warnings
  treated as errors. **All 52 upstream tests pass.**
* **Windows audio and MIDI.** WASAPI output by default, with every output
  device choosable. A winmm MIDI input engine reads every input or a chosen
  one, including pitch bend, program change and aftertouch. The choices are
  saved.
* **Graphics removed.** The FLTK, NTK and Zyn-Fusion interfaces, pixmaps,
  icons other than the program's own, and screenshots in the documentation
  are gone (about 22,000 lines). The synthesis code is unchanged.
* **The accessible interface**, generated from the engine's metadata:
  * a tree of the whole synthesizer, with parts named after their
    instruments and effect slots after their effect type;
  * a pane of standard controls per group, with values spoken in units;
  * editors for harmonics, envelope points, resonance and tuning;
  * action buttons;
  * a bank browser with preview, playing from the computer keyboard, and
    file, undo and audio/MIDI menus.
* **Checked through UI Automation** (`tools/uia_check.py --sweep`, run on a
  hidden desktop with `tools/hidden_desktop.py`). The sweep covered 64
  groups (Master, all of Part 1 with Kit item 1 and Voice 1, the effects,
  Tuning, Controllers and Settings) and 955 controls. After the navigation
  work: 151 groups, sections included, and 1,368 controls, again clean. Not one control was
  unnamed, and not one slider was without a value. Keys typed into sliders,
  the harmonic chooser and the bank browser were checked to reach the
  engine.
* **Release zip** (`tools/package.py`), including the banks, the manual
  (`doc/accessible/MANUAL.txt`) and the Visual C++ runtime. The GitHub
  Actions workflow "Windows (accessible edition)" builds, tests and packages
  on `windows-latest`. Its first run passed, and the zip is its artifact.

Added on 26 November 2026:

* **Navigation**:
  * Large groups are divided into sections. The sections appear in the
    tree, and also as headings in the group's own pane, where Ctrl+Down and
    Ctrl+Up jump between them.
  * Each group remembers the control last used in it, and F6 returns there.
  * Alt+Left and Alt+Right move to the same group of the previous or next
    voice, part, kit item, effect slot or band, keeping the setting.
  * Ctrl+F finds a setting by name, place or description.
* **The manual** (`doc/accessible/MANUAL.txt`, about 2,000 lines). It
  covers every key, menu, dialog, group and section, what the settings do,
  the effects and their presets, files, MIDI, OSC, the command line and
  troubleshooting. Its setting names were checked against UI Automation
  listings.
* **Smaller additions**:
  * the bank browser plays the selected instrument with Space;
  * the play keys are read by position, so they work on every keyboard
    layout;
  * automatic saving every minute, and restoring after a crash;
  * scale files in the File menu;
  * a notice when an engine is off in its kit item;
  * AddSynth's voice switches work;
  * many clearer names and values.

Still open:

* **Listening tests** with NVDA, JAWS and Narrator by a person, and sound
  checked by ear. Only the automated checks above have been run.
* **Value formatting (Phase 6)** covers units from the engine plus panning,
  detune, keys and times. Many 0 to 127 parameters still read as plain
  numbers where the engine has no unit for them.
* **The plugin (Phase 10)**: not started.
* **Not yet in the interface**:
  * MIDI learn;
  * copying and pasting presets of components;
  * adding and deleting free-mode envelope points (the engine keeps those
    ports internal).
  The manual says so where each would be.
* **Upstream's own "Build CI"** also runs on the fork. Its first run found
  missing plugin stubs. With those fixed (`9e6fc63`), every job passes,
  builds and tests included: Ubuntu gcc, Ubuntu clang with lld, and macOS
  clang. So the core still builds on Linux and macOS, even though only the
  Windows interface is the goal.

## 1. Verdict: is it doable?

Yes. Three facts about the existing code make it much smaller than it looks.

1. **The engine and the user interface are already separate programs in
   all but name.** The synth core never calls the UI. Everything the UI does
   is an OSC message (a path plus values, such as
   `/part0/kit0/adpars/GlobalPar/PVolume 100`) sent through `MiddleWare`.
   Upstream's own Zyn-Fusion UI is a separate process that talks to the engine
   over UDP. A new UI only has to implement five functions
   (`src/UI/Connection.h`: `createUi`, `destroyUi`, `raiseUi`, `tickUi`,
   `genOscInterface`) and send and receive those messages. The engine, the file
   formats, undo/redo (`/undo`, `/redo`), loading and saving (`/load_xmz`,
   `/save_xiz` and the rest) and the banks all stay untouched.
2. **Every parameter describes itself.** Each OSC port carries metadata: a
   short name (`rShort`, used about 380 times), a description (`rDoc` and the
   trailing string), a default (`rDefault`, about 380 times), a range
   (`rLinear`, `rLog`, `rMap(min/max)`), choice lists (`rOptions`, 64 lists),
   and in a few places a unit (`rUnit`, only 18 uses). The program can already
   write all of it out (`zynaddsubfx --dump-json-schema=FILE`). The probe in
   section 3 did this: 1,332 parameter kinds, every one with a description. An accessible UI
   can therefore be **generated from the metadata at run time**, instead of
   drawing 80-odd windows by hand. Parameters that upstream adds later then
   appear on their own.
3. **Upstream is already moving towards Windows and MSVC.** The latest commits
   remove variable-length arrays "for Windows" (`be7f9247`), replace pthreads,
   `dirent` and `unistd` with the C++ standard library (`3ab608c4`), and move
   to C++20 (`5f172dae`).

The hard parts are not the toolkit. They are:

* **Value formatting.** Most parameters are raw 0 to 127 numbers inside the
  engine. To speak "Attack 250 ms" or "Cutoff 1.2 kHz" rather than "64", the
  engine's own conversions have to be written out per parameter. Only 18 ports
  carry a unit today. This is the largest piece of manual work.
* **The handful of editors that really are pictures**: oscillator harmonics,
  free-mode envelopes, the resonance curve and the PADsynth harmonic profile.
  Each needs a non-graphical equivalent (see section 5).
* **Windows audio and MIDI input**, which today are minimal (section 2.4).

## 2. Where things stand

### 2.1 Version

* Upstream is `github.com/zynaddsubfx/zynaddsubfx`. Its `master` is at
  `c379439e` (10 September 2026), version **3.0.7 in development**.
* The last release, **3.0.6**, dates from 21 January 2022. `master` is 193
  commits ahead of it, including the Windows preparations above. **"Latest
  version" therefore means upstream `master`, not the 3.0.6 tag.**
* The local checkout (`C:\GIT\music\zynaddsubfx`) is a clean clone of
  upstream `master`, so it is already at the latest version.

### 2.2 The dengopaiv repository

**`dengopaiv/zynaddsubfx` already exists.** It is a public fork of upstream,
last pushed on 21 November 2025. It has **no commits of its own** and is
**20 commits behind** upstream `master`. Upgrading it is a plain
fast-forward. No new repository is needed unless you want a separate name
(decision D2 below).

### 2.3 The current user interfaces, and why none of them is accessible

* **FLTK UI** (`src/UI/*.fl` plus the `Fl_Osc_*` widgets): 16 FLUID files,
  12,543 lines, about 80 windows and about 33 custom-drawn graph widgets.
  FLTK draws its own widgets, so to UI Automation a FLTK window is one blank
  rectangle. It is the same problem as Tk, which the P-6 sample manager hit.
* **NTK UI**: the same code compiled against NTK, a FLTK fork. Same problem.
* **Zyn-Fusion ("zest")**: the default UI upstream. It is OpenGL plus mruby,
  lives in a separate repository (`mruby-zest-build`), and runs as a separate
  process. It is completely invisible to screen readers.
* **The plugin build** (`src/Plugin`, DPF: LV2, VST2, VST3, CLAP) uses the
  zest UI or none at all.

### 2.4 Windows support today

* **Audio**: PortAudio (`src/Nio/PaEngine.cpp`), but always the **default
  output device**. You cannot choose a device, a host API or a latency.
* **MIDI input**: a block in `src/main.cpp` commented "Windows MIDI OH WHAT A
  HACK...". It opens the first MIDI input that answers among device numbers 0
  to 9. You cannot choose a device. Note on, note off and controllers get
  through. **Pitch bend is ignored**, and so are program change, aftertouch
  and channel pressure.
* **Building**: upstream's only Windows instructions
  (`doc/win64-mingw64-msys2/README.txt`) are from 2016, use MinGW, and patch
  the CMake files with `sed`. The CMake files assume GCC flags (`-Wall -Wextra`,
  `-O3 -ffast-math`, `-msse`). Dependencies: zlib, fftw3 (single precision),
  mxml, liblo, PortAudio, plus the `rtosc` submodule. All five are in vcpkg.
* A build probe on this machine (section 3) shows that upstream `master` does
  not build with MSVC as it is, but does after about a dozen small fixes, and
  that the result passes 51 of the 52 tests.

### 2.5 Graphics in the program

* `pixmaps/`: knob, keyboard keys, window and module backdrops.
* FLTK graph widgets: `Fl_EQGraph`, `Fl_Oscilloscope`, `Fl_OscilSpectrum`,
  `Fl_Resonance_Graph`, `FormantFilterGraph`, `EnvelopeFreeEdit`,
  `Fl_PADnoteHarmonicProfile`, `Fl_PADnoteOvertonePosition`, VU meters and the
  virtual keyboard picture.
* The whole zest UI.

None of these hold anything the engine does not also hold as numbers. They
can all go. The few things they *show* that matter get a text equivalent
(section 5).

## 3. Build probe on this machine (25 November 2026)

The question was whether upstream `master` builds with MSVC at all. The test
was done on a throwaway copy of the source in a temporary folder. The checkout
in `C:\GIT\music\zynaddsubfx` was not changed, apart from initialising the
`rtosc` and `DPF` submodules.

Setup: Visual Studio 2026 (v18) Community, CMake 4.4, vcpkg at
`C:\GIT\environment\vcpkg`, triplet `x64-windows`. Packages installed by
vcpkg: `fftw3`, `mxml` (4.0.6), `liblo`, `portaudio` (19.7), `zlib`
(already there) and `getopt`. Configured with `GuiModule=off`, `Werror=OFF`.

### 3.1 Result: yes, with about a dozen small fixes

* **`rtosc` built with MSVC without a single source change**, tests included.
* With the fixes below, **the zyn core, `MiddleWare` and `zynaddsubfx.exe`
  build with MSVC**. vcpkg copies the six DLLs next to the exe.
* **`ctest`: 51 of 52 tests pass.** That covers all of rtosc's tests and 20
  of zyn's 21, among them AdNote, SubNote, PadNote, OscilGen, Middleware,
  Microtonal, Effect, Unison, Portamento, Plugin and XMLwrapper. The one that
  does not run is `KitTest` (fix 11 below).
* **`zynaddsubfx.exe -U -O PA` starts**: PortAudio opens the default output
  at 44.1 kHz with 256-sample buffers, the OSC server starts and the main loop
  runs. MIDI found no inputs (no MIDI device was connected, and the hack only
  tries device numbers 0 to 9). **Sound has not yet been checked by ear.**
* **`zynaddsubfx.exe -D schema.json` writes the parameter schema.** The
  numbers:
  * **1,332 distinct parameter kinds.** Counted with all 16 parts, 16 kit
    items, 8 voices and so on expanded, that is about 4.1 million addresses,
    which is why the UI must be built from the metadata and built only when
    needed.
  * Every kind has a description. Only 18 of those descriptions are empty or
    just repeat the name.
  * 1,147 have a short name, 1,076 a default, 913 a range and 152 a list of
    choices.
  * **Only 78 have a unit.** 900 numeric parameters have neither a unit nor a
    choice list. This confirms that value formatting (Phase 6) is the main
    manual work. AddSynth alone has 424 kinds, and 34 of them have a unit.
* The plugin targets (`ZynAddSubFX_vst`, `ZynAddSubFX_lv2`) still stop on a
  GCC-only flag (`-Wno-parentheses`). They were left for Phase 10.

### 3.2 What had to be fixed, which becomes Phase 2's to-do list

Build files:

1. `src/CMakeLists.txt` passes GCC flags to every compiler (`-Wall -Wextra`,
   and `-O3 -ffast-math -fomit-frame-pointer` in Release). MSVC stops on the
   first source file with `D8021: invalid numeric argument '/Wextra'`, so
   nothing compiles. Fix: wrap them in `if(NOT MSVC)` and use
   `/W3 /bigobj /utf-8 /fp:fast` under MSVC. The plugin's CMake file
   (`-Wno-parentheses`) needs the same fix.
2. Libraries are linked through a mix of old find modules and variables.
   `rtosc` wants the imported target `liblo::liblo`. zyn's `FindMXML` looks for
   `mxml`, but vcpkg's library is `mxml4.lib`. With
   `CMAKE_FIND_PACKAGE_PREFER_CONFIG` the build finds liblo but loses
   `ZLIB_LIBRARIES`. Fix: use a vcpkg manifest and imported targets
   (`ZLIB::ZLIB`, `liblo::liblo`, `PortAudio::portaudio`, and so on).
3. Windows builds link MinGW's `winpthread` (`src/CMakeLists.txt`) and the
   tests link `pthread` (`src/Tests/CMakeLists.txt`). Fix: link nothing extra
   under MSVC, and use `Threads::Threads` in the tests.

Source code (all small):

4. `src/main.cpp` uses `getopt_long` from `getopt.h`. The probe used vcpkg's
   `getopt` port. A small built-in argument parser would remove that DLL.
5. `src/Nio/ZynSema.h`: its Windows and macOS branch is written with pthreads.
   It was replaced by a `std::mutex` plus `std::condition_variable` version.
   `std::counting_semaphore` is not enough, because the queues read the count.
6. `src/DSP/Value_Smoothing_Filter.*` use GCC's `__restrict__`. MSVC spells
   it `__restrict`.
7. `src/Misc/Util.cpp` still uses POSIX `access(…, R_OK)` and `getpid()`.
   Fix: `std::filesystem` and `_getpid`.
8. `src/Misc/MiddleWare.cpp` still includes `<unistd.h>` (a leftover after
   upstream's STL rewrite). Its `#ifdef WIN32` branch for loading a part
   **calls an old `Part` constructor and no longer compiles**. That code has
   rotted, and the normal `std::async` branch works on Windows, so the
   Windows branch can simply go. This is worth reporting upstream.
9. `src/main.cpp` includes `<windows.h>` after `using namespace std`, so the
   Windows SDK's `byte` and C++17's `std::byte` collide. Fix: include it before
   any `using`, or move the Windows MIDI code into its own file (Phase 3 does
   this anyway).

Tests and tools:

10. `src/Tests/common.h` uses `_alloca` without including `<malloc.h>`.
11. `src/Tests/KitTest.cpp` uses `#define private public`. That cannot link
    under MSVC, because MSVC puts the access level into symbol names. The test
    needs a friend declaration or public test hooks.
12. The `--dump-json-schema` output is not valid JSON (`10.0f`, `0.f` inside
    ranges). It is easy to fix upstream, and worth fixing, because the
    accessible UI's tooling will read that file.

**Conclusion: the core is portable.** None of these fixes touches synthesis
or DSP code. Every one is the kind of change upstream has been accepting in
its own recent "for Windows" commits, so most can be offered upstream as pull
requests.

## 4. Decisions to make before Phase 1

**D1. Toolkit. Recommendation: plain Win32 standard controls, not wxWidgets.**

* Screambox, RubberDuck H3O+ and BlueWave already use plain Win32. BlueWave
  already solves the hard detail: trackbars that speak their value in units,
  through `IAccPropServices::SetHwndPropServer(PROPID_ACC_VALUE)`, names set
  with `SetHwndPropStr`, and an `announce()` helper (`src/app/main.cpp`,
  `src/app/valuectl.*`). That code can be reused almost as it is.
* No extra dependency to build, ship or keep up to date.
* On Windows, wxWidgets wraps the same native controls, so it gains nothing
  for accessibility. Its own widgets (`wxDataViewCtrl`, `wxGrid`, generic
  tree controls) are worse with screen readers than the native ones.
* Choose wxWidgets only if a Linux or macOS version becomes a goal. Even then,
  the metadata-driven design keeps the toolkit layer thin enough to port later.

**D2. Repository. Recommendation: reuse `dengopaiv/zynaddsubfx`.**

Keep its `master` as an exact mirror of upstream. Do all the work on a branch
called `accessible`, and make that the default branch. Merging new upstream
work is then `git merge upstream/master` into `accessible`. Rename the
repository (for example to `zynaddsubfx-accessible`) only if you want the
name to say what it is. GitHub keeps redirects from the old name.

**D3. Compiler. Recommendation: MSVC (Visual Studio 2026, already
installed), with vcpkg for the dependencies.**

This matches your other projects, and the probe in section 3 shows that MSVC
works. MinGW in MSYS2 remains a fallback. Upstream's own Windows builds have
historically been MinGW cross-builds.

**D4. Standalone first. A plugin later, or never.**

The standalone program covers playing and sound design. A VST3 or CLAP
plugin with this UI is possible later (Phase 10), because zyn can already
take its UI from another process over OSC.

**D5. Name and credit.**

Following the Screambox practice: a name that cannot be mistaken for an
upstream release, such as "ZynAddSubFX Accessible Edition", and an About box
and README that credit Nasca Octavian Paul, Mark McCurry and the upstream
contributors. The licence is GPL version 2 or later, which allows all of
this.

## 5. How the accessible UI works

### 5.1 Layout

One main window, three parts. F6 and Shift+F6 move between them.

1. **Menu bar**: File (new, open, save, open instrument, save instrument,
   load scale, recent files, exit), Edit (undo, redo, copy and paste preset,
   reset to default), Part, Options (audio and MIDI settings), Help (key list
   with F1, About).
2. **Structure tree** (a standard TreeView): Master, then Part 1 to 16, then
   kit items, then the engines (AddSynth: global, Voice 1 to 8 with oscillator,
   modulator, amplitude, frequency and filter sections, each with its envelope
   and LFO; SubSynth; PADsynth). Also: system effects 1 to 4, insertion
   effects 1 to 8, part effects 1 to 3, Microtonal, MIDI learn and Controllers.
   The tree is built from the port hierarchy, and nodes for disabled items
   (a voice that is switched off) say "off" in their name.
3. **Parameter pane** for the selected tree node, with one standard control
   per parameter, chosen from its metadata:
   * a continuous value is a **trackbar** whose accessible value is the
     formatted text ("Attack time, 250 ms");
   * a value from `rOptions` is a **combo box**;
   * an `rToggle` is a **check box**;
   * text (`rString`, the tuning text) is an **edit** box;
   * an action port (`randomize`, `convert to sine`, `apply PADsynth`) is a
     **button**.

### 5.2 Keyboard rules (to fill in during Phase 5)

* Enter on a slider opens a "type a value" box. Backspace resets a control to
  its `rDefault`. Ctrl+1 to Ctrl+9 jump to parts.
* Alt+Left and Alt+Right move to the previous or next voice, effect or part,
  and keep the same parameter focused. This comes from your BlueWave and
  Screambox habits and matters with 8 voices and 16 parts.
* A computer-keyboard "play" mode replaces the graphical virtual keyboard.
  It works like FLTK's: keys play notes, and the octave and velocity are
  spoken when they change.

### 5.3 Live updates and speech

* When MIDI, MIDI learn or undo changes a value, the engine broadcasts it.
  The UI updates the control without moving focus, and announces only a change
  to the control that has focus.
* Announcements use the BlueWave mechanism (UIA notification, MSAA name
  fallback). Short status messages: "Loaded bank Arpeggios, instrument 12",
  "Part 3 enabled", "Clipping on master" (from the VU data, which is otherwise
  not shown).

### 5.4 The metadata overlay

The generic UI reads the engine's port metadata. A hand-written overlay file
in the fork (`src/UI/Win32/overlay.*`) adds what the metadata lacks:

* a display name, where `rShort` is cryptic ("ph.rnd." becomes "Phase
  randomness");
* a unit and a conversion from raw value to text, for example `PAttackTime`
  0 to 127 to milliseconds, using the engine's own formulas from `src/Params`;
* the order and grouping of controls inside a pane;
* ports to hide (`rProp(internal)`, debug ports).

Wherever it can, the overlay should become **upstream contributions**: better
`rDoc`, `rShort`, `rUnit` and value-conversion metadata in zyn itself. Every
such change is one less difference to carry in the fork, and it also helps
Zyn-Fusion users.

### 5.5 Replacements for the graphical editors

| Graph today | Accessible replacement |
|---|---|
| Oscillator harmonics (128 magnitude and 128 phase bars) | A list view "Harmonic N: magnitude, phase", with sliders for the selected row. Buttons for the existing port actions: clear, convert to sine, normalize, base function. A "describe" command speaks the strongest harmonics. |
| Oscillator and spectrum display | Removed. On request, the program speaks its strongest harmonics. |
| Envelope in free mode (up to 40 points) | A list "Point N: time ms, value", with add, delete, time and value controls, and a choice of sustain point. |
| Resonance curve (256 points) | Sliders for centre frequency, octaves and gain, plus the existing operations (smooth, interpolate peaks, randomize, zero). A "point at frequency" slider pair edits single points. |
| PADsynth harmonic profile and overtone positions | The parameters that shape the graph are already ordinary ports. Only the picture goes. An "Apply" button triggers the PAD sample rebuild and announces when it is done. |
| EQ response curve | Removed. The bands stay as ordinary parameters, and on request the program speaks the band gains. |
| Formant filter graph | Removed. Formants and vowels stay as list and parameter panes. |
| VU meters | Removed. Clipping is announced, and a key speaks the peak level of the master and of each part. |
| Microtonal editor | Already text: the `tunings` and `mapping` ports take and return the user's text. Two edit boxes, plus loading `.scl` and `.kbm` files. |
| Bank browser (pictures of 128 slots) | A list of banks and a list of instruments, with type-to-search. Enter loads the instrument into the current part. |

## 6. Phases

Each phase ends with something that works and can be tested.

### Phase 0. Plan and feasibility (this document)

* Done: this roadmap, the repository check, and the build probe (section 3).
  The probe's patched copy and build are in the session scratchpad and are
  disposable. The fixes will be redone properly in Phase 2.
* Exit: decisions D1 to D5 made.

### Phase 1. Repository and upgrade

* Fast-forward the fork's `master` to upstream `master`, which upgrades it to
  the latest version. Create the `accessible` branch and make it the default.
* Add `upstream` as a remote locally. Note in the README how to merge from
  upstream.
* Add GitHub Actions on `windows-latest`: configure with vcpkg, build with
  MSVC, run `ctest`.
* Exit: CI builds the untouched upstream code, or shows the failures that
  Phase 2 will fix.

### Phase 2. Headless build with MSVC

* Make the CMake files compiler-neutral: GCC-only flags (`-Wall -Wextra`,
  `-ffast-math`, `-msse`, `-O3`) only under GCC or Clang, `/W3 /fp:fast
  /arch:AVX2` (or `/arch:SSE2`) under MSVC.
* Use a vcpkg manifest (`vcpkg.json` already exists with `liblo` and needs
  the rest added) and the vcpkg toolchain file.
* Make the twelve fixes in section 3.2 properly in the source. The probe made
  them as quick stand-ins (a forced-include header, an empty `unistd.h`,
  macros), and those are not to be kept.
* Send the generally useful ones upstream: the CMake MSVC flags, `ZynSema`,
  `__restrict__`, the stale `WIN32` branch in `MiddleWare.cpp`, and the JSON
  schema output.
* Exit: `zynaddsubfx.exe -U` (no UI) starts, loads an instrument with
  `-L file.xiz`, and is heard playing from a MIDI keyboard. All 52 tests pass
  under MSVC, and CI builds it.

### Phase 3. Windows audio and MIDI

* Replace the `main.cpp` MIDI hack with a proper `WinMidiEngine` in
  `src/Nio` (a `MidiIn` subclass): choose the device by name, handle every
  channel message including pitch bend, aftertouch and program change, and
  allow several inputs.
* Give `PaEngine` device and host-API selection (WASAPI by default, ASIO if
  vcpkg's PortAudio is built with it), buffer size and sample rate, saved in
  zyn's config.
* Expose both through OSC ports, so the UI's Options dialog can use them.
* Exit: choose a device, play, hear it at low latency, and bend pitch.

### Phase 4. Remove the graphics

* Delete the FLTK and NTK UI (`src/UI/*.fl`, `Fl_*`, `guimain.cpp`,
  `NSM*`, `pixmaps/`), the zest launcher code in `main.cpp`, the
  `ZynFusionDir` and `GuiModule` options except `off` and the new `win32`,
  and the X11 and OpenGL lookups.
* Keep `zynaddsubfx.ico` for the program icon.
* Leave the core (`src/Synth`, `src/Params`, `src/Effects`, `src/DSP`,
  `src/Misc`) untouched, so upstream merges stay clean. A merge in which
  upstream changed a deleted UI file is resolved by keeping it deleted.
* Exit: the build has no FLTK, NTK, OpenGL or X11 dependency.

### Phase 5. The Win32 UI skeleton

* `src/UI/Win32/`: implement the five `GUI::` functions; give the main window
  its menu bar, tree and pane; drive the MiddleWare tick from a Win32 timer so
  that nothing blocks the audio thread.
* Walk the port tree. Build controls from the metadata. Read values with
  `requestValue`, write them with `writeValue`, and receive broadcasts.
* Bring over BlueWave's value-annotation and announce code.
* Exit: with NVDA, every scalar parameter of Part 1 can be reached and set,
  and the change is heard. The same works under JAWS and Narrator.

### Phase 6. Names, units and order (the overlay)

* Work through, in order: Part and Master, AddSynth global and voice, the
  filters, the envelopes, the LFOs, SubSynth, PADsynth, the effects (each
  effect's named parameters, `rEffPar`, and its presets), and Controllers.
* For each parameter: a display name, a unit and conversion, and a group.
* Send generally useful metadata fixes upstream as small pull requests.
* Exit: no control speaks a bare 0 to 127 number where the engine means a
  time, frequency, level or ratio.

### Phase 7. Everyday workflow

* Bank browser, open and save (`.xmz` master, `.xiz` instrument, `.xsz` and
  `.scl`/`.kbm` scales), recent files, autosave restore (already in
  `MiddleWare`).
* Undo and redo, preset copy and paste (`PresetsUI` equivalent), reset to
  default.
* Part management: enable, MIDI channel, key range, polyphony, and the kit
  editor (kit items, their engines and their key ranges).
* MIDI learn: list the mappings, learn a new one, delete.
* Computer-keyboard play mode.
* Exit: a complete session (load a bank instrument, edit it, map a
  controller, save, reopen) without sighted help.

### Phase 8. The special editors

* Harmonics, free-mode envelopes, resonance, microtonal and PADsynth apply,
  as in section 5.5.
* Exit: everything the FLTK UI could edit can be edited.

### Phase 9. Testing, documentation, release

* An accessibility checklist, run with NVDA, JAWS and Narrator on every pane.
* Automated UI Automation tests in Python, like BlueWave's `tests/*_gui.py`.
  They walk every tree node and check that every control has a name, a role
  and a value that is not a bare number.
* A plain-text manual and a key list on F1.
* Packaging: a zip with `zynaddsubfx.exe`, its DLLs (or the vcpkg
  `x64-windows-static` triplet for a single exe), the `instruments`
  submodule's banks, and the README.
* Exit: version 1.0 of the accessible edition is released.

### Phase 10 (optional). Plugin

* zyn already runs its UI out of process over OSC (`zynaddsubfx-ext-gui`).
  The Win32 UI can be built the same way, as an external program that
  connects to a plugin instance in a DAW such as REAPER.
* Alternatively, a native HWND editor embedded through DPF.
* Decide only after Phase 9.

## 7. Risks and how to keep them small

* **MSVC and the core or `rtosc`.** The probe largely settled this (section 3):
  rtosc needed no change and the core about a dozen small ones. Remaining
  unknowns: sound quality under `/fp:fast` compared with GCC's `-ffast-math`
  (compare test renders, since the tests already render notes), and the
  plugin targets. Fallback: MinGW/UCRT64, which is already installed in
  `C:\msys64`.
* **Upstream drift.** Keep every change outside `src/UI/Win32`, `src/Nio` and
  the CMake files small, and send it upstream where possible. Merge upstream
  regularly, not once a year.
* **Size of the parameter tree.** The generic UI makes the size cheap. The
  overlay (Phase 6) is where the hours go, and it can be done one engine at a
  time while everything else already works.
* **Real-time safety.** The UI only ever talks to `MiddleWare`, never to
  `Master`. This is the existing rule, and keeping to it means a slow
  screen-reader query can never cause an audio dropout.
* **Performance of the tree.** 16 parts × 16 kit items × 8 voices is far too
  many controls to create at once. Build tree nodes when they are expanded
  and panes when they are shown, and destroy them when they are left.

## 8. First concrete steps, once you say go

1. Decide D1 to D5. Each recommendation above is a sensible default.
2. Fast-forward `dengopaiv/zynaddsubfx`, create `accessible`, add Windows CI
   (Phase 1).
3. Make the headless MSVC build pass (Phase 2), starting from the errors
   listed in section 3.
