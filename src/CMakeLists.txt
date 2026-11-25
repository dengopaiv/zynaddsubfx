set (CMAKE_CXX_STANDARD 20)

#checking include/library paths
message(STATUS "Checking Include Path" $ENV{CMAKE_INCLUDE_PATH} ${CMAKE_INCLUDE_PATH})
message(STATUS "Checking Library Path" $ENV{CMAKE_LIBRARY_PATH} ${CMAKE_LIBRARY_PATH})

#Dependency check
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckCXXCompilerFlag)
if(NOT (${CMAKE_SYSTEM_NAME} STREQUAL "Windows"))
    find_package(PkgConfig)
endif()
find_package(ZLIB REQUIRED)
find_package(Threads   REQUIRED)
find_package(OSS)
find_package(Alsa)
find_package(Sndio)
find_package(ECM)
list(APPEND CMAKE_MODULE_PATH ${ECM_FIND_MODULE_DIR})
if(CompileTests AND ${CMAKE_SYSTEM_NAME} STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
    # LibGit2's find_package seems to complain if the version.h header does not exist
    # So let's search for it before even calling find_package...
    if(EXISTS "/usr/include/git2/version.h")
        find_package(LibGit2)
    else()
        message(STATUS "Could NOT find LibGit2 (missing file: /usr/include/git2/version.h)")
    endif()
endif()
if(PKG_CONFIG_FOUND AND NOT (${CMAKE_SYSTEM_NAME} STREQUAL "Windows"))
    message("Looking For pkg config modules")
    pkg_check_modules(JACK jack)
    pkg_check_modules(PORTAUDIO portaudio-2.0>=19)

    pkg_check_modules(FFTW3F REQUIRED fftw3f)
    pkg_check_modules(MXML mxml4)
    if(NOT MXML_FOUND)
        pkg_check_modules(MXML REQUIRED mxml)
    endif()

    pkg_search_module(LASH lash-1.0)
    mark_as_advanced(LASH_LIBRARIES)
    pkg_search_module(DSSI dssi>=0.9.0)
    mark_as_advanced(DSSI_LIBRARIES)
    pkg_search_module(LIBLO liblo>=0.26 REQUIRED)
    mark_as_advanced(LIBLO_LIBRARIES)
else()
    find_package(FFTW3F REQUIRED)
    find_package(MXML REQUIRED)
    find_package(LIBLO REQUIRED)
    find_package(PORTAUDIO)
endif()

CHECK_FUNCTION_EXISTS(sched_setscheduler HAVE_SCHEDULER)
CHECK_CXX_SOURCE_COMPILES(
    "#include <future>
    #if ATOMIC_INT_LOCK_FREE<2
    #error unreliable atomics
    #endif
    int main(){return 0;}" HAVE_ASYNC)

check_include_file_cxx(complex HAVE_CPP_STD_COMPLEX)

find_library(HAVE_LIBRT rt)

set(CMAKE_REQUIRED_FLAGS "")


CHECK_CXX_SOURCE_COMPILES(
    "int main(){
    __asm__ __volatile__ (\"fistpl 0\");
    return 0;}" HAVE_X86_FISTPL)

######### Settings ###########
# NOTE: These cache variables should normally not be changed in this
# file, but either in in CMakeCache.txt before compile, or by passing
# parameters directly into cmake using the -D flag.
# The graphical FLTK, NTK and Zyn-Fusion (zest) interfaces were removed:
# the only user interface is the accessible Win32 one (GuiModule win32,
# added with the UI itself) or none (off).
if(WIN32)
    SET (GuiModule win32 CACHE STRING "GUI module: win32 (accessible) or off")
else()
    SET (GuiModule off CACHE STRING "GUI module: off")
endif()
SET (CompileTests ON CACHE BOOL "whether tests should be compiled in or not")
SET (CompileExtensiveTests OFF CACHE BOOL "whether tests that take a long time should be compiled in or not")
SET (AlsaEnable ${ALSA_FOUND} CACHE BOOL
    "Enable support for Advanced Linux Sound Architecture")
SET (JackEnable ${JACK_FOUND} CACHE BOOL
    "Enable support for JACK Audio Connection toolKit")
SET (OssEnable ${OSS_FOUND} CACHE BOOL
    "Enable support for Open Sound System")
SET (PaEnable ${PORTAUDIO_FOUND} CACHE BOOL
    "Enable support for Port Audio System")
SET (SndioEnable ${SNDIO_FOUND} CACHE BOOL
    "Enable support for Sndio System")
SET (LashEnable ${LASH_FOUND} CACHE BOOL
    "Enable LASH Audio Session Handler")
SET (DssiEnable ${DSSI_FOUND} CACHE BOOL
    "Enable DSSI Plugin compilation")
SET (NoNeonPlease False CACHE BOOL
    "Workaround For Broken Neon Detection")
SET (PluginLibDir "lib" CACHE STRING
    "Install directory for plugin libraries PREFIX/PLUGIN_LIB_DIR/{lv2,vst}")
SET (DemoMode FALSE CACHE BOOL "Enable 10 minute silence")
if(MSVC)
    # the DPF plugin build files still assume GCC/Clang flags
    SET (PluginEnable FALSE CACHE BOOL "Enable Plugins")
else()
    SET (PluginEnable TRUE CACHE BOOL "Enable Plugins")
endif()

# Now, handle the incoming settings and set define flags/variables based
# on this

# Add version information
add_definitions(-DVERSION="${VERSION}")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  add_definitions(-static)
endif()

if(HAVE_SCHEDULER AND (NOT "Darwin" STREQUAL ${CMAKE_SYSTEM_NAME}))
    # Add scheduler function existence info (OSX compatibility)
    add_definitions(-DHAVE_SCHEDULER=${HAVE_SCHEDULER})
endif()

if(HAVE_BG_SYNTH_THREAD)
    add_definitions(-DHAVE_BG_SYNTH_THREAD=1)
else()
    add_definitions(-DHAVE_BG_SYNTH_THREAD=0)
endif()

if(HAVE_ASYNC)
    add_definitions(-DHAVE_ASYNC=1)
else()
    add_definitions(-DHAVE_ASYNC=0)
endif()

if(HAVE_CPP_STD_COMPLEX)
    add_definitions(-DHAVE_CPP_STD_COMPLEX=1)
endif()

if(DemoMode)
    add_definitions(-DDEMO_VERSION=1)
endif()


# Give a good guess on the best Input/Output default backends
if (JackEnable)
    SET (DefaultOutput jack CACHE STRING
        "Default Output module: [null, alsa, oss, jack, portaudio]")
    # Override with perhaps more helpful midi backends
    if (AlsaEnable)
        SET (DefaultInput alsa CACHE STRING
            "Default Input module: [null, alsa, oss, jack]")
    elseif (OssEnable)
        SET (DefaultInput oss CACHE STRING
            "Default Input module: [null, alsa, oss, jack]")
    else ()
        SET (DefaultInput jack CACHE STRING
            "Default Input module: [null, alsa, oss, jack]")
    endif ()
elseif (AlsaEnable)
    SET (DefaultOutput alsa CACHE STRING
        "Default Output module: [null, alsa, oss, jack, portaudio]")
    SET (DefaultInput alsa CACHE STRING
        "Default Input module: [null, alsa, oss, jack]")
elseif (OssEnable)
    SET (DefaultOutput oss CACHE STRING
        "Default Output module: [null, alsa, oss, jack, portaudio]")
    SET (DefaultInput oss CACHE STRING
        "Default Input module: [null, alsa, oss, jack]")
elseif (PaEnable)
    SET (DefaultOutput pa CACHE STRING
        "Default Output module: [null, alsa, oss, jack, portaudio]")
    if (WIN32)
        SET (DefaultInput winmidi CACHE STRING
            "Default Input module: [null, winmidi]")
    else ()
        SET (DefaultInput null CACHE STRING
            "Default Input module: [null, alsa, oss, jack]")
    endif ()
elseif (SndioEnable)
    SET (DefaultOutput sndio CACHE STRING
        "Default Output module: [null, alsa, oss, jack, portaudio, sndio]")
    SET (DefaultInput sndio CACHE STRING
        "Default Input module: [null, alsa, oss, jack, sndio]")
else()
    SET (DefaultOutput null CACHE STRING
        "Default Output module: [null, alsa, oss, jack, portaudio]")
    SET (DefaultInput null CACHE STRING
        "Default Input module: [null, alsa, oss, jack]")
endif()



if(GuiModule STREQUAL win32 AND WIN32)
    set(Win32Gui TRUE)
    add_definitions(-DWIN32_UI=1)
elseif(NOT GuiModule STREQUAL off)
    message(STATUS "GUI module \"${GuiModule}\" is not available, using off")
    set (GuiModule off CACHE STRING "GUI module: off" FORCE)
endif()
if(NOT Win32Gui)
    add_definitions(-DDISABLE_GUI)
endif()


#Build Flags
option (BuildForAMD_X86_64 "Build for AMD x86_64 system" OFF)
option (BuildForCore2_X86_64 "Build for Intel Core2 x86_64 system" OFF)
option (BuildForDebug "Include gdb debugging support" OFF)
option (ExtendedWarnings "Enable all useful warnings" OFF)
option (Werror "Treat all warnings as errors" ON)
option (IncludeWhatYouUse "Check for useless includes" OFF)
mark_as_advanced(IncludeWhatYouUse)

set(CMAKE_BUILD_TYPE "Release")

set(ZynBuildOptions_ExtendedWarnings)
if (ExtendedWarnings)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # The following generates a list of warning exceptions
        # (after it enables all warnings).
        # This is for developers who want to make sure they really
        # see all possible warnings, with a few exceptions that don't
        # make sense in this code base.
        # You can remove those exceptions if they don't complain too
        # often about the code base. If you want to add exceptions,
        # be VERY CAREFUL, ask the project maintainers for
        # their consent.
        list (APPEND ZynBuildOptions_ExtendedWarnings
        "-Wextra" "-Weverything" # start with really everything
        "-Wno-c++98-compat" "-Wno-c++98-compat-extra-semi" "-Wno-c++98-compat-pedantic" # C++11 is a zyn build requirement
        "-Wno-cast-align" # not relevant for our case
        "-Wno-double-promotion" # Used too often to fix
        "-Wno-old-style-cast" # forces you to use static_cast<> etc everywhere! too many errors
        "-Wno-padded" # this tells you to realign your structs to have better memory management
        "-Wno-zero-as-null-pointer-constant" # wants you to use nullptr in the whole code base
        "-Wno-implicit-int-conversion" # too often to fix
        "-Wno-extra-semi" # one semicolon more than useful - no danger to disable this
        "-Wno-vla-extension" "-Wno-vla" # rtosc currently heavily uses them, and they are supported by our CI
        "-Wno-exit-time-destructors" "-Wno-global-constructors" # used too often in rtosc
        "-Wno-gnu-zero-variadic-macro-arguments" # used too often in zyn
        "-Wno-shadow" "-Wno-shadow-field" # used too often in zyn
        "-Wno-cast-align" # used too often in zyn
        "-Wno-missing-prototypes" # used too often in zyn
        "-Wno-sign-conversion" # used too often in zyn
        "-Wno-float-equal" # can be disturbing in some cases
        "-Wno-switch-enum" # used too often in zyn
        "-Wno-unsafe-buffer-usage" # used too often in zyn
        "-Wimplicit-int-float-conversion" # used too often in zyn
        )
        MESSAGE(STATUS "Using extended Warning options: ${ZynBuildOptions_ExtendedWarnings}")
    else()
        MESSAGE(WARNING "\"ExtendedWarnings\" selected, but this is only supported for clang - ignoring")
    endif()
endif()

set (ZynBuildOptions_x86_64AMD
    -march=athlon64;-m64
  CACHE STRING "X86_64 compiler options"
)

set (ZynBuildOptions_X86_64Core2
    -march=core2;-m64
  CACHE STRING "X86_64 compiler options"
)

set (ZynBuildOptions_NEON
    -march=armv7-a;-mfloat-abi=hard;-mfpu=neon;-mcpu=cortex-a9;-mtune=cortex-a9;-pipe;-mvectorize-with-neon-quad;-funsafe-loop-optimizations
  CACHE STRING "Cortex_a9 compiler options"
)

check_cxx_compiler_flag("${ZynBuildOptions_NEON} -Werror" SUPPORT_NEON)

set (ZynBuildOptions_SSE
    -msse;-msse2;-mfpmath=sse
  CACHE STRING "SSE compiler options"
)

check_cxx_compiler_flag("${ZynBuildOptions_SSE} -Werror" SUPPORT_SSE)

set (ZynBuildOptionsBasic
    -O3;-ffast-math;-fomit-frame-pointer
    CACHE STRING "basic X86 compiler options"
)
list(APPEND ZynBuildOptionsBasic ${ZynBuildOptions_ExtendedWarnings})

set (ZynBuildOptionsDebug
    -O0;-g3;-ggdb;-Wpointer-arith
    CACHE STRING "Debug build flags")
list(APPEND ZynBuildOptionsDebug ${ZynBuildOptions_ExtendedWarnings})

if(IncludeWhatYouUse)
    find_program(IwyuPath NAMES include-what-you-use iwyu)
    if(NOT IwyuPath)
        set(IwyuErr "package NOT found")
    endif()
else()
    set(IwyuErr "disabled (IncludeWhatYouUse=OFF)")
endif()

########### Settings dependent code ###########
# From here on, the setting variables have  been prepared so concentrate
# on the actual compiling.

# To avoid a conflict if PortAudio v1 is installed, the linker directory
# of portaudio must be specified first, so that xxx/lib/portaudio2 is
# searched before xxx/lib where PortAudio v1 gets installed.
if(PaEnable)
	include_directories(${PORTAUDIO_INCLUDE_DIRS})
	add_definitions(-DPORTAUDIO=1)
	list(APPEND AUDIO_LIBRARIES ${PORTAUDIO_LIBRARIES})
	list(APPEND AUDIO_LIBRARY_DIRS ${PORTAUDIO_LIBRARY_DIRS})
endif()

if(AlsaEnable)
	list(APPEND AUDIO_LIBRARIES ${ASOUND_LIBRARY})
	list(APPEND AUDIO_LIBRARY_DIRS ${ASOUND_LIBRARY_DIRS})
	add_definitions(-DALSA=1)
endif(AlsaEnable)

if(JackEnable)
	list(APPEND AUDIO_LIBRARIES ${JACK_LIBRARIES})
	list(APPEND AUDIO_LIBRARY_DIRS ${JACK_LIBRARY_DIRS})
	add_definitions(-DJACK=1)
endif(JackEnable)

if(OssEnable)
	add_definitions(-DOSS=1)
endif(OssEnable)

if(SndioEnable)
	list(APPEND AUDIO_LIBRARIES ${SNDIO_LIBRARY})
	list(APPEND AUDIO_LIBRARY_DIRS ${SNDIO_LIBRARY_DIRS})
	add_definitions(-DSNDIO=1)
endif(SndioEnable)

if (CompileTests)
	ENABLE_TESTING()
endif()

if(LashEnable)
	include_directories(${LASH_INCLUDE_DIRS})
	add_definitions(-DLASH=1)
	list(APPEND AUDIO_LIBRARIES ${LASH_LIBRARIES})
	list(APPEND AUDIO_LIBRARY_DIRS ${LASH_LIBRARY_DIRS})
	message(STATUS "Compiling with lash")
endif()

include_directories(${LIBLO_INCLUDE_DIRS})
add_definitions(-DUSE_NSM=1)
list(APPEND AUDIO_LIBRARIES ${LIBLO_LIBRARIES})
list(APPEND AUDIO_LIBRARY_DIRS ${LIBLO_LIBRARY_DIRS})
message(STATUS "Compiling with liblo")

# other include directories
include_directories(${ZLIB_INCLUDE_DIRS} ${MXML_INCLUDE_DIRS} ${FFTW3F_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR}/src) # for zyn-version.h ...


if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_definitions(-DWIN32)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "CYGWIN")
    add_definitions(-D_GNU_SOURCE=1 -D_POSIX_THREAD_PRIO_INHERIT=1)
endif()

if(MSVC)
    # C4244/C4267: implicit narrowing conversions, which GCC's -Wall -Wextra
    # do not report either; the code base relies on them throughout
    add_compile_options(/W3 /bigobj /utf-8 /fp:fast /wd4244 /wd4267)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    # main.cpp uses getopt_long(), which MSVC's C library lacks
    find_path(GETOPT_INCLUDE_DIR getopt.h REQUIRED)
    find_library(GETOPT_LIBRARY getopt REQUIRED)
    include_directories(${GETOPT_INCLUDE_DIR})
else()
    add_compile_options(-Wall -Wextra)
endif()
# Check for -W... and if available, add compile option -Wno-...
function (zyn_disable_warning W_FLAG)
    if(MSVC)
        return()
    endif()
    string(TOUPPER "${W_FLAG}" CACHE_VAR)
    string(REPLACE "-" "_" CACHE_VAR "${CACHE_VAR}")
    set(_CACHE_VAR "HAVE_WARNING_${CACHE_VAR}")
    check_cxx_compiler_flag("-W${W_FLAG}" "${CACHE_VAR}")
    if (${${CACHE_VAR}})
        add_compile_options("-Wno-${W_FLAG}")
    endif()
endfunction ()

zyn_disable_warning("stringop-truncation")
zyn_disable_warning("vla-cxx-extension")
zyn_disable_warning("nan-infinity-disabled")
zyn_disable_warning("inconsistent-missing-override")

if(HAVE_X86_FISTPL)
    message(STATUS "Compiling with x86 opcode support")
    add_definitions(-DASM_F2I_YES)
endif()

if (BuildForDebug)
	set (CMAKE_BUILD_TYPE "Debug")
	add_compile_options("$<$<CONFIG:Debug>:${ZynBuildOptionsDebug}>")
else (BuildForDebug)
	set (CMAKE_BUILD_TYPE "Release")
	
	if(NOT MSVC)
		add_compile_options("$<$<CONFIG:Release>:${ZynBuildOptionsBasic}>")
	endif()

	if (BuildForAMD_X86_64)
		add_compile_options($<$<CONFIG:Release>:${ZynBuildOptions_x86_64AMD}>)
	endif (BuildForAMD_X86_64)
	
	if (BuildForCore2_X86_64)
		add_compile_options($<$<CONFIG:Release>:${ZynBuildOptions_X86_64Core2}>)
	endif (BuildForCore2_X86_64)

	if (SUPPORT_SSE AND NOT MSVC)
		add_compile_options("$<$<CONFIG:Release>:${ZynBuildOptions_SSE}>")
	endif (SUPPORT_SSE)
	
	if (SUPPORT_NEON AND NOT NoNeonPlease AND NOT MSVC)
		add_compile_options($<$<CONFIG:Release>:${ZynBuildOptions_NEON}>)
	endif (SUPPORT_NEON AND NOT NoNeonPlease)

endif (BuildForDebug)
message (STATUS "Building for ${CMAKE_BUILD_TYPE}, flags: ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")

if(NOT (${CMAKE_SYSTEM_NAME} STREQUAL "Windows"))
    add_definitions(-fPIC)
endif()


# No user interface: stubs, plus the (non-graphical) NSM session client.
# The tests link this one.
add_library(zynaddsubfx_gui_bridge STATIC UI/ConnectionDummy.cpp
    UI/NSM.C UI/NSM/Client.C)
add_definitions(-DNO_UI=1)

# The accessible Windows interface (standard Win32 controls)
if(Win32Gui)
    add_library(zynaddsubfx_win32_ui STATIC
        UI/Win32/Win32Ui.cpp
        UI/Win32/PortModel.cpp
        UI/Win32/Overlay.cpp
        UI/Win32/Access.cpp
        UI/NSM.C UI/NSM/Client.C)
    target_link_libraries(zynaddsubfx_win32_ui comctl32 comdlg32 oleacc
        uiautomationcore ole32 oleaut32 shell32)
    set(UI_BRIDGE zynaddsubfx_win32_ui)
    set(UI_RESOURCES UI/Win32/zynui.rc UI/Win32/zynui.manifest)
else()
    set(UI_BRIDGE zynaddsubfx_gui_bridge)
    set(UI_RESOURCES)
endif()

########### General section ##############
# Following this should be only general compilation code, and no mention
# of module-specific variables

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/zyn-version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/zyn-version.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/zyn-config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/zyn-config.h)

link_directories(${AUDIO_LIBRARY_DIRS} ${ZLIB_LIBRARY_DIRS} ${FFTW3F_LIBRARY_DIRS} ${MXML_LIBRARY_DIRS})

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	)


set(NONGUI_LIBRARIES
	zynaddsubfx_misc
	zynaddsubfx_synth
	zynaddsubfx_effect
	zynaddsubfx_params
	zynaddsubfx_dsp
	zynaddsubfx_nio
	)

add_subdirectory(Misc)
add_subdirectory(Synth)
add_subdirectory(Effects)
add_subdirectory(Params)
add_subdirectory(DSP)
add_subdirectory(Nio)
if (PluginEnable)
    add_subdirectory(Plugin)
endif()

add_library(zynaddsubfx_core STATIC
    version.cpp
    globals.cpp
    ../tlsf/tlsf.c
    Containers/ScratchString.cpp
    Containers/NotePool.cpp
    Containers/MultiPseudoStack.cpp
	${zynaddsubfx_dsp_SRCS}
	${zynaddsubfx_effect_SRCS}
	${zynaddsubfx_misc_SRCS}
	${zynaddsubfx_params_SRCS}
	${zynaddsubfx_synth_SRCS}
	)

if(MSVC)
    # std::thread etc. need no extra library with MSVC
    set(PTHREAD_LIBRARY "")
    set(PLATFORM_LIBRARIES ws2_32 winmm iphlpapi ${GETOPT_LIBRARY})
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(PTHREAD_LIBRARY winpthread)
    set(PLATFORM_LIBRARIES ws2_32
            winmm
            wsock32
            "-static" iphlpapi
            "-static" winpthread)
elseif(APPLE)
    set(PTHREAD_LIBRARY pthread)
else()
    if(HAVE_LIBRT)
        set(PLATFORM_LIBRARIES rt)
    endif()
    set(PTHREAD_LIBRARY pthread)
endif()

if(ZYN_SYSTEM_RTOSC)
    target_link_libraries(zynaddsubfx_core
        ${ZLIB_LIBRARIES}
        ${FFTW3F_LIBRARIES}
        ${MXML_LIBRARIES}
        ${OS_LIBRARIES}
        ${PTHREAD_LIBRARY}
        ${RTOSC_LIBRARIES}
        ${RTOSC_CPP_LIBRARIES}
    )
else()
    target_link_libraries(zynaddsubfx_core
        ${ZLIB_LIBRARIES}
        ${FFTW3F_LIBRARIES}
        ${MXML_LIBRARIES}
        ${OS_LIBRARIES}
        ${PTHREAD_LIBRARY}
        rtosc
        rtosc-cpp
    )
endif()

if(IwyuErr)
    message (STATUS "Include what you use: ${IwyuErr}")
else()
    set(IwyuPathAndOptions
        ${IwyuPath}
        -Xiwyu
        --no_comments)
    set_property(TARGET zynaddsubfx_core PROPERTY CXX_INCLUDE_WHAT_YOU_USE
                 ${IwyuPathAndOptions})
endif()

if(CompileTests)
	add_subdirectory(Tests)
endif(CompileTests)


add_executable(zynaddsubfx main.cpp ${UI_RESOURCES})
if(Win32Gui)
    # a window, not a console; main() stays the entry point
    set_target_properties(zynaddsubfx PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(zynaddsubfx PRIVATE /ENTRY:mainCRTStartup)
endif()
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/instruments/banks")
    # the instrument banks next to the program, as in the release package
    add_custom_command(TARGET zynaddsubfx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/instruments/banks"
            "$<TARGET_FILE_DIR:zynaddsubfx>/banks")
endif()

if(Werror)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        foreach(target zynaddsubfx zynaddsubfx_core zynaddsubfx_nio zynaddsubfx_gui_bridge)
            set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
        endforeach()
    else()
        if(MSVC)
            target_compile_options(zynaddsubfx PUBLIC /WX)
        else()
            target_compile_options(zynaddsubfx PUBLIC -Werror)
        endif()
    endif()
endif()

#Warning: the required ordering of these to get proper linking depends upon the
#         phase of the moon
target_link_libraries(zynaddsubfx
	zynaddsubfx_core
	zynaddsubfx_nio
	${UI_BRIDGE}
	${GUI_LIBRARIES}
	${NIO_LIBRARIES}
	${AUDIO_LIBRARIES}
	${PLATFORM_LIBRARIES}
)

if (DssiEnable)
	add_library(zynaddsubfx_dssi SHARED
			UI/ConnectionDummy.cpp
			Output/DSSIaudiooutput.cpp
			globals.cpp
			Output/DSSIControl.cpp
			Output/DSSIControl.h
			Output/DSSIControlDescription.cpp
			Output/DSSIControlDescription.h)

	target_link_libraries(zynaddsubfx_dssi
		zynaddsubfx_core
		${OS_LIBRARIES}
		${LIBLO_LIBRARIES}
		${PLATFORM_LIBRARIES}
		-Wl,--no-undefined
		)
    install(TARGETS zynaddsubfx_dssi LIBRARY DESTINATION ${PluginLibDir}/dssi/)
endif()

install(TARGETS zynaddsubfx
	RUNTIME DESTINATION bin
	)

include(CTest)


##Summarize The Full Configuration
message(STATUS)
message(STATUS "${ColorBold}ZynAddSubFX Build Configuration")
message(STATUS             "===============================${ColorReset}")
message(STATUS)
message(STATUS "Building on a '${CMAKE_SYSTEM_NAME}' System")


macro(package_status foundvar pkg state)#optional color
    set (extra_macro_args ${ARGN})
    list(LENGTH extra_macro_args num_extra_args)
    if (${num_extra_args} GREATER 0)
        list(GET extra_macro_args 0 color)
    endif ()
    if(${foundvar})
        message(STATUS "${pkg} -- ${Green}${state}${ColorReset}")
    else()
        message(STATUS "${pkg} -- ${color}NOT ${state}${ColorReset}")
    endif()
endmacro()

package_status(PKG_CONFIG_FOUND "PkgConfig" "found"   ${Red})
package_status(ZLIB_FOUND       "zlib     " "found"   ${Red})
package_status(MXML_FOUND       "mxml     " "found"   ${Red})
package_status(FFTW3F_FOUND     "fftw3f   " "found"   ${Red})
package_status(LIBLO_FOUND      "liblo    " "found"   ${Red})
package_status(OSS_FOUND        "OSS      " "found"   ${Yellow})
package_status(ALSA_FOUND       "ALSA     " "found"   ${Yellow})
package_status(JACK_FOUND       "JACK     " "found"   ${Yellow})
package_status(PORTAUDIO_FOUND  "PA       " "found"   ${Yellow})
package_status(SNDIO_FOUND      "SNDIO    " "found"   ${Yellow})
package_status(LASH_FOUND       "Lash     " "found"   ${Yellow})
package_status(DSSI_FOUND       "DSSI     " "found"   ${Yellow})
package_status(LashEnable       "Lash     " "enabled" ${Yellow})
package_status(DssiEnable       "DSSI     " "enabled" ${Yellow})
package_status(CompileTests     "tests    " "enabled" ${Yellow})
package_status(AlsaEnable       "ALSA     " "enabled" ${Yellow})
package_status(JackEnable       "JACK     " "enabled" ${Yellow})
package_status(OssEnable        "OSS      " "enabled" ${Yellow})
package_status(PaEnable         "PA       " "enabled" ${Yellow})
package_status(SndioEnable      "SNDIO    " "enabled" ${Yellow})
package_status(HAVE_BG_SYNTH_THREAD "Background synthesizer thread" "enabled" ${Yellow})
message(STATUS "GUI module: ${GuiModule}")
package_status(HAVE_ASYNC       "c++ async" "usable"  ${Yellow})

message(STATUS "Link libraries: ${ZLIB_LIBRARY} ${FFTW3F_LIBRARY} ${MXML_LIBRARIES} ${AUDIO_LIBRARIES} ${OS_LIBRARIES}")
