/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "maploader.h"
#include "numeric_safety.h"
#include "own_track_transition_linkage.h"
#include "repeater_linkage.h"
#include "text_decoder.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"

#include <windows.h>
#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#endif
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

struct NewElementTemplateCategoryInfo {
    const char* id;
    NewElementTemplateCategory category;
    const char* label_key;
};

constexpr std::array<NewElementTemplateCategoryInfo, 7>
    k_new_element_template_categories = {{
        {"scenery", NewElementTemplateCategory::Scenery, "aux.scenery"},
        {"station", NewElementTemplateCategory::Station, "aux.station"},
        {"track_geometry", NewElementTemplateCategory::TrackGeometry,
         "aux.track_geometry"},
        {"other_track", NewElementTemplateCategory::OtherTrack,
         "frame.othertracks"},
        {"signal", NewElementTemplateCategory::Signal, "aux.signal"},
        {"sound", NewElementTemplateCategory::Sound, "aux.sound"},
        {"effects", NewElementTemplateCategory::Effects, "aux.effects"},
    }};

struct NewFileTemplateCategoryInfo {
    const char* id;
    const char* label_key;
};

struct NewFileTemplate {
    NewFileKind kind;
    const char* category_id;
    const char* label;
    const char* usage_key;
};

constexpr std::array<NewFileTemplateCategoryInfo, 3>
    k_new_file_template_categories = {{
        {"presets", "new_file.category.presets"},
        {"map", "new_file.category.map"},
        {"resource_lists", "new_file.category.resource_lists"},
    }};

constexpr std::array<NewFileTemplate, 6> k_new_file_templates = {{
    {NewFileKind::Map, "map", "BveTs Map 2.02", "new_file.usage.map"},
    {NewFileKind::Structure, "resource_lists", "Structure", "new_file.usage.structure"},
    {NewFileKind::Signal, "resource_lists", "Signal", "new_file.usage.signal"},
    {NewFileKind::Sound, "resource_lists", "Sound", "new_file.usage.sound"},
    {NewFileKind::Sound3D, "resource_lists", "Sound3D", "new_file.usage.sound3d"},
    {NewFileKind::Station, "resource_lists", "Station", "new_file.usage.station"},
}};

const std::vector<NewElementTemplate>& new_element_templates_internal() {
    static const std::vector<NewElementTemplate> templates = {
        {
            "structure.put", NewElementTemplateCategory::Scenery, 0,
            "structure.put", "Put",
            "Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span);",
            "new_element.usage.structure.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"x", "x", MapElementNumericConstraint::Truncate3, true, "0"},
                {"y", "y", MapElementNumericConstraint::Truncate3, true, "0"},
                {"z", "z", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rx", "rx", MapElementNumericConstraint::Truncate3, true, "0"},
                {"ry", "ry", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rz", "rz", MapElementNumericConstraint::Truncate3, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Tilt, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
            },
        },
        {
            "structure.put0", NewElementTemplateCategory::Scenery, 1,
            "structure.put", "Put0",
            "Structure[structureKey].Put0(trackKey, tilt, span);",
            "new_element.usage.structure.put0", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Tilt, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
            },
        },
        {
            "repeater.begin", NewElementTemplateCategory::Scenery, 3,
            "repeater", "Begin",
            "Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...);",
            "new_element.usage.repeater.begin", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"endDistance", "endDistance", MapElementNumericConstraint::Finite, true, "0"},
                {"repeaterKey", "repeaterKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"x", "x", MapElementNumericConstraint::Truncate3, true, "0"},
                {"y", "y", MapElementNumericConstraint::Truncate3, true, "0"},
                {"z", "z", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rx", "rx", MapElementNumericConstraint::Truncate3, true, "0"},
                {"ry", "ry", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rz", "rz", MapElementNumericConstraint::Truncate3, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Tilt, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
                {"interval", "interval", MapElementNumericConstraint::Finite, true, "25"},
            },
        },
        {
            "repeater.begin0", NewElementTemplateCategory::Scenery, 4,
            "repeater", "Begin0",
            "Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...);",
            "new_element.usage.repeater.begin0", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"endDistance", "endDistance", MapElementNumericConstraint::Finite, true, "0"},
                {"repeaterKey", "repeaterKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Tilt, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
                {"interval", "interval", MapElementNumericConstraint::Finite, true, "25"},
            },
        },
        {
            "structure.put_between", NewElementTemplateCategory::Scenery, 2,
            "structure.between", "",
            "Structure[structureKey].PutBetween(trackKey1, trackKey2, flag);",
            "new_element.usage.structure.put_between", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey1", "trackKey1", MapElementNumericConstraint::None, true, "0"},
                {"trackKey2", "trackKey2", MapElementNumericConstraint::None, true, "0"},
                {"flag", "flag", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "station.put", NewElementTemplateCategory::Station, 0,
            "station.put", "",
            "Station[stationKey].Put(door, margin1, margin2);",
            "new_element.usage.station.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"stationKey", "stationKey", MapElementNumericConstraint::None, true, ""},
                {"door", "door", MapElementNumericConstraint::Door, true, "0"},
                {"margin1", "back", MapElementNumericConstraint::Finite, true, "0"},
                {"margin2", "front", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "signal.put", NewElementTemplateCategory::Signal, 0,
            "signal.put", "",
            "Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span);",
            "new_element.usage.signal.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"signalAspectKey", "signalAspectKey", MapElementNumericConstraint::None, true, ""},
                {"section", "section", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"x", "x", MapElementNumericConstraint::Truncate3, true, "0"},
                {"y", "y", MapElementNumericConstraint::Truncate3, true, "0"},
                {"z", "z", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rx", "rx", MapElementNumericConstraint::Truncate3, true, "0"},
                {"ry", "ry", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rz", "rz", MapElementNumericConstraint::Truncate3, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Tilt, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
            },
        },
        {
            "speedlimit.begin", NewElementTemplateCategory::Signal, 6,
            "speedlimit", "Begin",
            "SpeedLimit.Begin(speed);",
            "new_element.usage.speedlimit.begin", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"speed", "speed", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "speedlimit.end", NewElementTemplateCategory::Signal, 7,
            "speedlimit", "End",
            "SpeedLimit.End();",
            "new_element.usage.speedlimit.end", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.begin", NewElementTemplateCategory::Signal, 1,
            "section.begin", "Begin",
            "Section.Begin(signal0, ..., signalN);",
            "new_element.usage.section.begin", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.beginnew", NewElementTemplateCategory::Signal, 2,
            "section.begin", "BeginNew",
            "Section.BeginNew(signal0, ..., signalN);",
            "new_element.usage.section.beginnew", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.setspeedlimit", NewElementTemplateCategory::Signal, 3,
            "section.speedLimit", "SetSpeedLimit",
            "Section.SetSpeedLimit(v0, ..., vn);",
            "new_element.usage.section.setspeedlimit", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.signal_speedlimit", NewElementTemplateCategory::Signal, 4,
            "section.speedLimit", "Signal.SpeedLimit",
            "Signal.SpeedLimit(v0, ..., vn);",
            "new_element.usage.section.signal_speedlimit", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "curve", NewElementTemplateCategory::TrackGeometry, 0,
            "curve", "",
            "Curve.*",
            "new_element.usage.curve", false,
            {
                {"transitionStart", "BeginTr Dist.##CurveStartTransition",
                 MapElementNumericConstraint::Finite, true, "0"},
                {"distance", "distance##CurveStartDistance",
                 MapElementNumericConstraint::Finite, true, "0"},
                {"method", "method", MapElementNumericConstraint::None, true, "Begin"},
                {"radius", "radius", MapElementNumericConstraint::Finite, true, "0"},
                {"cant", "cant", MapElementNumericConstraint::Finite, true, "0"},
                {"endTransitionStart", "BeginTr Dist.##CurveEndTransition",
                 MapElementNumericConstraint::Finite, true, "0"},
                {"endDistance", "distance##CurveEndDistance",
                 MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "gradient", NewElementTemplateCategory::TrackGeometry, 1,
            "gradient", "",
            "Gradient.*",
            "new_element.usage.gradient", false,
            {
                {"transitionStart", "BeginTr Dist.##GradientStartTransition",
                 MapElementNumericConstraint::Finite, true, "0"},
                {"distance", "distance##GradientStartDistance",
                 MapElementNumericConstraint::Finite, true, "0"},
                {"gradient", "gradient", MapElementNumericConstraint::Finite, true, "0"},
                {"endTransitionStart", "BeginTr Dist.##GradientEndTransition",
                 MapElementNumericConstraint::Finite, true, "0"},
                {"endDistance", "distance##GradientEndDistance",
                 MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "irregularity.change", NewElementTemplateCategory::TrackGeometry, 2,
            "irregularity.change", "",
            "Irregularity.Change(x, y, r, lx, ly, lr);",
            "new_element.usage.irregularity.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"x", "x", MapElementNumericConstraint::Finite, true, "0"},
                {"y", "y", MapElementNumericConstraint::Finite, true, "0"},
                {"r", "r", MapElementNumericConstraint::Finite, true, "0"},
                {"lx", "lx", MapElementNumericConstraint::Finite, true, "0"},
                {"ly", "ly", MapElementNumericConstraint::Finite, true, "0"},
                {"lr", "lr", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "other_track.position", NewElementTemplateCategory::OtherTrack, 0,
            "otherTrack.change", "Track.Position",
            "Track[trackKey].Position(x, y, radiusH, radiusV);",
            "new_element.usage.other_track.position", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "x", MapElementNumericConstraint::Finite, true, "0"},
                {"parameter1", "y", MapElementNumericConstraint::Finite, true, "0"},
                {"parameter2", "radiusH", MapElementNumericConstraint::Finite, true, "0", true},
                {"parameter3", "radiusV", MapElementNumericConstraint::Finite, true, "0", true},
            },
        },
        {
            "other_track.x_interpolate", NewElementTemplateCategory::OtherTrack, 1,
            "otherTrack.change", "Track.X.Interpolate",
            "Track[trackKey].X.Interpolate(x, radius);",
            "new_element.usage.other_track.x_interpolate", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "x", MapElementNumericConstraint::Finite, true, "0", true},
                {"parameter1", "radius", MapElementNumericConstraint::Finite, true, "0", true},
            },
        },
        {
            "other_track.y_interpolate", NewElementTemplateCategory::OtherTrack, 2,
            "otherTrack.change", "Track.Y.Interpolate",
            "Track[trackKey].Y.Interpolate(y, radius);",
            "new_element.usage.other_track.y_interpolate", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "y", MapElementNumericConstraint::Finite, true, "0", true},
                {"parameter1", "radius", MapElementNumericConstraint::Finite, true, "0", true},
            },
        },
        {
            "other_track.cant_set_gauge", NewElementTemplateCategory::OtherTrack, 3,
            "otherTrack.change", "Track.Cant.SetGauge",
            "Track[trackKey].Cant.SetGauge(gauge);",
            "new_element.usage.other_track.cant_set_gauge", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "gauge", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "other_track.cant_set_center", NewElementTemplateCategory::OtherTrack, 4,
            "otherTrack.change", "Track.Cant.SetCenter",
            "Track[trackKey].Cant.SetCenter(x);",
            "new_element.usage.other_track.cant_set_center", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "x", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "other_track.cant_set_function", NewElementTemplateCategory::OtherTrack, 5,
            "otherTrack.change", "Track.Cant.SetFunction",
            "Track[trackKey].Cant.SetFunction(id);",
            "new_element.usage.other_track.cant_set_function", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "id", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "other_track.cant_begin_transition", NewElementTemplateCategory::OtherTrack, 6,
            "otherTrack.change", "Track.Cant.BeginTransition",
            "Track[trackKey].Cant.BeginTransition();",
            "new_element.usage.other_track.cant_begin_transition", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
            },
        },
        {
            "other_track.cant_begin", NewElementTemplateCategory::OtherTrack, 7,
            "otherTrack.change", "Track.Cant.Begin",
            "Track[trackKey].Cant.Begin(cant);",
            "new_element.usage.other_track.cant_begin", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "cant", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "other_track.cant_end", NewElementTemplateCategory::OtherTrack, 8,
            "otherTrack.change", "Track.Cant.End",
            "Track[trackKey].Cant.End();",
            "new_element.usage.other_track.cant_end", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
            },
        },
        {
            "other_track.cant_interpolate", NewElementTemplateCategory::OtherTrack, 9,
            "otherTrack.change", "Track.Cant.Interpolate",
            "Track[trackKey].Cant.Interpolate(cant);",
            "new_element.usage.other_track.cant_interpolate", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, ""},
                {"parameter0", "cant", MapElementNumericConstraint::Finite, true, "0", true},
            },
        },
        {
            "beacon.put", NewElementTemplateCategory::Signal, 5,
            "beacon.put", "",
            "Beacon.Put(type, section, sendData);",
            "new_element.usage.beacon.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"type", "type", MapElementNumericConstraint::Finite, true, "0"},
                {"section", "section", MapElementNumericConstraint::Finite, true, "0"},
                {"sendData", "sendData", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "map_sound.play", NewElementTemplateCategory::Sound, 0,
            "mapSound.play", "",
            "Sound[soundKey].Play();",
            "new_element.usage.map_sound.play", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"soundKey", "soundKey", MapElementNumericConstraint::None, true, ""},
            },
        },
        {
            "map_sound3d.put", NewElementTemplateCategory::Sound, 1,
            "mapSound3D.put", "",
            "Sound3D[soundKey].Put(x, y);",
            "new_element.usage.map_sound3d.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"soundKey", "soundKey", MapElementNumericConstraint::None, true, ""},
                {"x", "x", MapElementNumericConstraint::Finite, true, "0"},
                {"y", "y", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "rolling_noise.change", NewElementTemplateCategory::Sound, 2,
            "rollingNoise.change", "",
            "RollingNoise.Change(index);",
            "new_element.usage.rolling_noise.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"index", "index", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "flange_noise.change", NewElementTemplateCategory::Sound, 3,
            "flangeNoise.change", "",
            "FlangeNoise.Change(index);",
            "new_element.usage.flange_noise.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"index", "index", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "joint_noise.play", NewElementTemplateCategory::Sound, 4,
            "jointNoise.play", "",
            "JointNoise.Play(index);",
            "new_element.usage.joint_noise.play", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"index", "index", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "background.change", NewElementTemplateCategory::Effects, 0,
            "background.change", "",
            "Background.Change(structureKey);",
            "new_element.usage.background.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
            },
        },
        {
            "adhesion.change", NewElementTemplateCategory::TrackGeometry, 7,
            "adhesion.change", "",
            "Adhesion.Change(a); / Adhesion.Change(a, b, c);",
            "new_element.usage.adhesion.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"a", "a", MapElementNumericConstraint::Finite, true, "0"},
                {"b", "b", MapElementNumericConstraint::Finite, false, ""},
                {"c", "c", MapElementNumericConstraint::Finite, false, ""},
            },
        },
        {
            "cab_illuminance.set", NewElementTemplateCategory::Effects, 1,
            "cabIlluminance.change", "Set",
            "CabIlluminance.Set(value);",
            "new_element.usage.cab_illuminance.set", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"value", "value", MapElementNumericConstraint::Finite, false, ""},
            },
        },
        {
            "cab_illuminance.interpolate", NewElementTemplateCategory::Effects, 2,
            "cabIlluminance.change", "Interpolate",
            "CabIlluminance.Interpolate(value);",
            "new_element.usage.cab_illuminance.interpolate", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"value", "value", MapElementNumericConstraint::Finite, false, ""},
            },
        },
        {
            "fog.set", NewElementTemplateCategory::Effects, 3,
            "fog.change", "Set",
            "Fog.Set(density, red, green, blue);",
            "new_element.usage.fog.set", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"density", "density", MapElementNumericConstraint::Finite, true, "0"},
                {"red", "red", MapElementNumericConstraint::Finite, true, "0"},
                {"green", "green", MapElementNumericConstraint::Finite, true, "0"},
                {"blue", "blue", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "fog.interpolate", NewElementTemplateCategory::Effects, 4,
            "fog.change", "Interpolate",
            "Fog.Interpolate(); / Fog.Interpolate(density); / Fog.Interpolate(density, red, green, blue);",
            "new_element.usage.fog.interpolate", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"density", "density", MapElementNumericConstraint::Finite, false, ""},
                {"red", "red", MapElementNumericConstraint::Finite, false, ""},
                {"green", "green", MapElementNumericConstraint::Finite, false, ""},
                {"blue", "blue", MapElementNumericConstraint::Finite, false, ""},
            },
        },
        {
            "draw_distance.change", NewElementTemplateCategory::Effects, 5,
            "drawDistance.change", "",
            "DrawDistance.Change(value);",
            "new_element.usage.draw_distance.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"value", "value", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
    };
    return templates;
}

} // namespace

const std::vector<NewElementTemplate>& new_element_templates() {
    return new_element_templates_internal();
}

bool new_element_template_accepts_key_source(
    const NewElementTemplate& tpl, MapElementKeySource key_source) {
    if (key_source == MapElementKeySource::None) return false;
    for (const NewElementFieldSpec& field : tpl.fields) {
        if (map_element_key_source_for_field(tpl.row_kind, field.key) == key_source) {
            return true;
        }
    }
    // Repeater structure keys are appended to the shared form after its
    // fixed template fields are built.
    return map_element_key_source_for_field(
               tpl.row_kind, "structureKeys.0") == key_source;
}

const std::vector<size_t>& new_element_template_display_order() {
    static const std::vector<size_t> display_order = [] {
        const std::vector<NewElementTemplate>& templates = new_element_templates();
        std::vector<size_t> indices;
        indices.reserve(templates.size());
        for (size_t index = 0; index < templates.size(); ++index) {
            indices.push_back(index);
        }
        std::stable_sort(indices.begin(), indices.end(),
                         [&](size_t lhs_index, size_t rhs_index) {
            const NewElementTemplate& lhs = templates[lhs_index];
            const NewElementTemplate& rhs = templates[rhs_index];
            if (lhs.category != rhs.category) {
                return static_cast<int>(lhs.category) < static_cast<int>(rhs.category);
            }
            return lhs.category_order < rhs.category_order;
        });
        return indices;
    }();
    return display_order;
}

void render_new_element_usage_text(std::string_view text) {
    // ImGui's normal word wrapping treats an uninterrupted CJK run as one
    // word. Split non-ASCII code points into individually breakable tokens so
    // localized descriptions can use the available list width naturally.
    // The child window's built-in padding remains the narrow safe gap at both
    // edges, so descriptions can otherwise use the full available width.
    const float wrap_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const char* const text_end = text.data() + text.size();
    const auto flush_line = [](std::string& line) {
        if (line.empty()) return;
        ImGui::TextUnformatted(line.c_str());
        line.clear();
    };

    std::string line;
    const char* pending_space_begin = nullptr;
    const char* pending_space_end = nullptr;
    for (const char* cursor = text.data(); cursor < text_end;) {
        const char* const token_begin = cursor;
        unsigned int codepoint = 0;
        const int codepoint_size = ImTextCharFromUtf8(&codepoint, cursor, text_end);
        if (codepoint_size <= 0) break;
        const char* token_end = cursor + codepoint_size;

        if (codepoint == '\r') {
            cursor = token_end;
            continue;
        }
        if (codepoint == '\n') {
            flush_line(line);
            pending_space_begin = nullptr;
            pending_space_end = nullptr;
            cursor = token_end;
            continue;
        }
        if (codepoint == ' ' || codepoint == '\t') {
            if (!line.empty()) {
                if (!pending_space_begin) pending_space_begin = token_begin;
                pending_space_end = token_end;
            }
            cursor = token_end;
            continue;
        }

        // Keep ASCII words intact, while allowing CJK code points to wrap at
        // character boundaries. This preserves normal English word wrapping.
        if (codepoint < 0x80) {
            while (token_end < text_end) {
                unsigned int next_codepoint = 0;
                const int next_size =
                    ImTextCharFromUtf8(&next_codepoint, token_end, text_end);
                if (next_size <= 0 || next_codepoint >= 0x80 ||
                    next_codepoint == ' ' || next_codepoint == '\t' ||
                    next_codepoint == '\r' || next_codepoint == '\n') {
                    break;
                }
                token_end += next_size;
            }
        }

        const size_t previous_line_size = line.size();
        if (!line.empty() && pending_space_begin) {
            line.append(pending_space_begin,
                        static_cast<size_t>(pending_space_end - pending_space_begin));
        }
        line.append(token_begin, static_cast<size_t>(token_end - token_begin));
        if (previous_line_size > 0 &&
            ImGui::CalcTextSize(line.c_str(), nullptr, false).x > wrap_width) {
            line.resize(previous_line_size);
            flush_line(line);
            line.assign(token_begin, static_cast<size_t>(token_end - token_begin));
        }
        pending_space_begin = nullptr;
        pending_space_end = nullptr;
        cursor = token_end;
    }
    flush_line(line);
}

bool new_element_target_is_resource_list(
    const MapModel& model, const std::string& file_path) {
    for (const ResourceListSource& source : model.resource_list_sources) {
        if (source.present && !source.resolved_path.empty() &&
            source.resolved_path == file_path) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> new_element_target_candidates(const MapModel& model) {
    std::map<std::string, size_t> distance_counts;
    for (const EditStatementInfo& statement : model.edit_statements) {
        if (statement.source.file_path.empty() ||
            new_element_target_is_resource_list(model, statement.source.file_path)) {
            continue;
        }
        // Distance.Set is the parser's internal classification for a source
        // line whose expression sets the current BVE distance. It is not a
        // BVE source function and must never be emitted as source text.
        if (statement.statement_kind != "Distance.Set") continue;
        ++distance_counts[statement.source.file_path];
    }

    std::vector<std::pair<std::string, size_t>> ranked;
    ranked.reserve(model.edit_files.size());
    for (const EditSourceFileInfo& file : model.edit_files) {
        auto count = distance_counts.find(file.file_path);
        if (file.file_path.empty() || new_element_target_is_resource_list(model, file.file_path)) {
            continue;
        }
        ranked.emplace_back(file.file_path,
                            count == distance_counts.end() ? 0 : count->second);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& lhs, const auto& rhs) {
                         if (lhs.second != rhs.second) return lhs.second > rhs.second;
                         return lhs.first < rhs.first;
                     });

    std::vector<std::string> result;
    result.reserve(ranked.size());
    for (const auto& entry : ranked) result.push_back(entry.first);
    return result;
}

std::vector<std::string> new_file_reference_target_candidates(const MapModel& model) {
    // Official BVE treats Include and every *.Load directive as ordinary map
    // statements: none of them depends on the current distance. The New Map
    // Element wizard likewise accepts every editable map source and delegates
    // sparse source placement to maploader's tail-distance-block path.
    std::vector<std::string> result;
    for (const EditSourceFileInfo& file : model.edit_files) {
        if (file.file_path.empty() ||
            new_element_target_is_resource_list(model, file.file_path)) {
            continue;
        }
        result.push_back(file.file_path);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void update_repeater_wizard_field_enablement(NewElementWizardState& wizard) {
    if (wizard.form.row_kind != "repeater") return;
    for (MapElementEditFieldState& field : wizard.form.fields) {
        if (field.key == "repeaterKey") {
            field.disabled = false;
        } else if (field.key == "endDistance") {
            field.disabled = !wizard.repeater_add_end;
        } else {
            field.disabled = !wizard.repeater_add_begin;
        }
    }
}

bool is_own_track_wizard(const NewElementWizardState& wizard) {
    return wizard.form.row_kind == "curve" || wizard.form.row_kind == "gradient";
}

std::string own_track_curve_method(const NewElementWizardState& wizard) {
    const MapElementEditFieldState* method =
        find_inspector_field(wizard.form, "method");
    return method ? edit_field_buffer_text(*method) : std::string{"Begin"};
}

void update_own_track_wizard_field_enablement(NewElementWizardState& wizard) {
    if (!is_own_track_wizard(wizard)) return;
    const bool curve = wizard.form.row_kind == "curve";
    const bool change = curve && own_track_curve_method(wizard) == "Change";
    if (wizard.own_track_add_start && change) {
        wizard.own_track_start_add_transition = false;
        wizard.own_track_curve_add_cant = false;
    }
    if (wizard.own_track_add_start && curve && !change) {
        wizard.own_track_curve_add_cant =
            wizard.own_track_start_add_transition;
    }
    if (wizard.own_track_add_start && wizard.own_track_add_end) {
        wizard.own_track_end_add_transition =
            wizard.own_track_start_add_transition;
    }

    for (MapElementEditFieldState& field : wizard.form.fields) {
        if (field.key == "transitionStart") {
            const bool was_disabled = field.disabled;
            field.disabled = !wizard.own_track_add_start ||
                !wizard.own_track_start_add_transition || change;
            if (was_disabled && !field.disabled &&
                edit_field_buffer_text(field) == field.original_value) {
                const MapElementEditFieldState* distance =
                    find_inspector_field(wizard.form, "distance");
                if (distance) {
                    field.original_value = edit_field_buffer_text(*distance);
                    set_edit_field_buffer(field, field.original_value);
                }
            }
        } else if (field.key == "distance" || field.key == "method" ||
                   field.key == "radius" || field.key == "gradient") {
            field.disabled = !wizard.own_track_add_start;
        } else if (field.key == "cant") {
            field.disabled = !wizard.own_track_add_start || change ||
                !wizard.own_track_start_add_transition ||
                !wizard.own_track_curve_add_cant;
        } else if (field.key == "endTransitionStart") {
            const bool was_disabled = field.disabled;
            field.disabled = !wizard.own_track_add_end ||
                !wizard.own_track_end_add_transition;
            if (was_disabled && !field.disabled &&
                edit_field_buffer_text(field) == field.original_value) {
                const MapElementEditFieldState* distance =
                    find_inspector_field(wizard.form, "endDistance");
                if (distance) {
                    field.original_value = edit_field_buffer_text(*distance);
                    set_edit_field_buffer(field, field.original_value);
                }
            }
        } else if (field.key == "endDistance") {
            field.disabled = !wizard.own_track_add_end;
        }
    }
}

bool App::can_use_resource_key_in_new_element_wizard(
    MapElementKeySource key_source) const {
    if (!edit_actions_available() || key_source == MapElementKeySource::None) {
        return false;
    }

    const std::vector<NewElementTemplate>& templates = new_element_templates();
    const NewElementWizardState& wizard = new_element_wizard_;
    if (wizard.selected_template < 0 ||
        wizard.selected_template >= static_cast<int>(templates.size())) {
        return false;
    }
    const NewElementTemplate& tpl =
        templates[static_cast<size_t>(wizard.selected_template)];
    if (!new_element_template_accepts_key_source(tpl, key_source)) {
        return false;
    }
    if (!wizard.open) return true;

    // Do not rebuild an open wizard here: the normal wizard render pass owns
    // template transitions, and rebuilding here would discard its draft.
    if (wizard.built_template != wizard.selected_template || !wizard.form.open ||
        std::string_view(wizard.form.row_kind) != tpl.row_kind) {
        return false;
    }
    return std::any_of(
        wizard.form.fields.begin(), wizard.form.fields.end(),
        [key_source](const MapElementEditFieldState& field) {
            return field.key_source == key_source && !field.read_only &&
                !field.disabled;
        });
}

bool App::use_resource_key_in_new_element_wizard(
    MapElementKeySource key_source, std::string_view key) {
    const std::string key_copy(key);
    if (blank_ascii(key_copy) ||
        !can_use_resource_key_in_new_element_wizard(key_source)) {
        return false;
    }

    NewElementWizardState& wizard = new_element_wizard_;
    if (!wizard.open) {
        open_new_element_wizard();
        rebuild_new_element_wizard_form();
    }
    const auto field = std::find_if(
        wizard.form.fields.begin(), wizard.form.fields.end(),
        [key_source](const MapElementEditFieldState& candidate) {
            return candidate.key_source == key_source &&
                !candidate.read_only && !candidate.disabled;
        });
    if (field == wizard.form.fields.end()) return false;

    set_edit_field_buffer(*field, key_copy);
    return true;
}

void App::open_new_element_wizard(std::optional<double> distance_prefill) {
    if (distance_prefill && !std::isfinite(*distance_prefill)) {
        distance_prefill.reset();
    }

    NewElementWizardState& wizard = new_element_wizard_;
    const bool initialize = !distance_prefill || !wizard.open;
    if (initialize) {
        wizard.target_file_path.clear();
        wizard.target_file_candidates.clear();
        wizard.target_candidates_built = false;
        wizard.built_template = -1;
        wizard.built_target_file.clear();
        wizard.repeater_change_point_prompt_requested = false;
        wizard.repeater_extra_end_prompt_requested = false;
        wizard.confirm_repeater_change_point_once = false;
        wizard.return_inspector_request.reset();
        wizard.close_after_successful_apply = false;
        wizard.apply_then_open_created_element = false;
    }
    wizard.open = true;
    wizard.distance_prefill = distance_prefill;
    if (!initialize && distance_prefill) {
        apply_new_element_wizard_distance_prefill();
    }
}

bool App::prepare_repeater_wizard_from_inspector(std::string& repeater_key) {
    if (!inspector_.open || inspector_.row_kind != "repeater" || inspector_.edit_id.empty()) {
        return false;
    }

    size_t row_index = 0;
    if (!find_row_index_by_edit_id(model_.repeaters, inspector_.edit_id, row_index)) {
        KME_ADD_LOG("[warn]Repeater wizard lost its source row: " +
                inspector_.edit_id);
        return false;
    }
    const TableRow& repeater_row = model_.repeaters[row_index];
    const std::string source_file = !repeater_row.source.file_path.empty()
        ? repeater_row.source.file_path
        : inspector_.source_file;
    repeater_key = table_cell(repeater_row, "repeaterKey");
    if (source_file.empty() || repeater_key.empty()) {
        KME_ADD_LOG("[warn]Repeater wizard is missing source metadata");
        return false;
    }

    const std::vector<NewElementTemplate>& templates = new_element_templates();
    const char* template_id = inspector_.source_zero_offset_method
        ? "repeater.begin0"
        : "repeater.begin";
    const auto selected = std::find_if(
        templates.begin(), templates.end(),
        [&](const NewElementTemplate& tpl) { return tpl.id == std::string_view(template_id); });
    if (selected == templates.end()) {
        KME_ADD_LOG("[error]Repeater wizard template is unavailable");
        return false;
    }

    std::vector<std::string> target_candidates = new_element_target_candidates(model_);
    if (std::find(target_candidates.begin(), target_candidates.end(), source_file) ==
        target_candidates.end()) {
        // Keep the physical Begin source selected even when it has no numeric
        // distance anchor. Maploader will retain its existing insertion validation
        // rather than silently redirecting the new statement to another source file.
        target_candidates.insert(target_candidates.begin(), source_file);
    }

    open_new_element_wizard();
    NewElementWizardState& wizard = new_element_wizard_;
    wizard.selected_template = static_cast<int>(std::distance(templates.begin(), selected));
    wizard.target_file_path = source_file;
    wizard.target_file_candidates = std::move(target_candidates);
    wizard.target_candidates_built = true;
    wizard.built_template = -1;
    wizard.built_target_file.clear();
    wizard.return_inspector_request = make_inspector_reload_request(inspector_);
    wizard.close_after_successful_apply = true;
    rebuild_new_element_wizard_form();

    if (!find_inspector_field(wizard.form, "distance") ||
        !find_inspector_field(wizard.form, "endDistance") ||
        !find_inspector_field(wizard.form, "repeaterKey")) {
        KME_ADD_LOG("[error]Repeater wizard fields are unavailable");
        wizard.open = false;
        wizard.return_inspector_request.reset();
        wizard.close_after_successful_apply = false;
        wizard.apply_then_open_created_element = false;
        return false;
    }
    return true;
}

void App::open_repeater_end_wizard_from_inspector() {
    if (!inspector_.open || inspector_.row_kind != "repeater" ||
        inspector_.repeater_boundary_kind != "open" || inspector_.edit_id.empty()) {
        return;
    }

    std::string repeater_key;
    if (!prepare_repeater_wizard_from_inspector(repeater_key)) return;

    NewElementWizardState& wizard = new_element_wizard_;

    wizard.repeater_add_begin = false;
    wizard.repeater_add_end = true;
    update_repeater_wizard_field_enablement(wizard);

    MapElementEditFieldState* key_field =
        find_inspector_field(wizard.form, "repeaterKey");
    MapElementEditFieldState* end_distance_field =
        find_inspector_field(wizard.form, "endDistance");
    if (!key_field || !end_distance_field) {
        KME_ADD_LOG("[error]Repeater End wizard fields are unavailable");
        wizard.open = false;
        wizard.return_inspector_request.reset();
        wizard.close_after_successful_apply = false;
        wizard.apply_then_open_created_element = false;
        return;
    }
    key_field->original_value = repeater_key;
    set_edit_field_buffer(*key_field, repeater_key);
    end_distance_field->original_value.clear();
    set_edit_field_buffer(*end_distance_field, {});
}

void App::open_repeater_change_point_wizard_from_inspector() {
    std::string source_repeater_key;
    if (!prepare_repeater_wizard_from_inspector(source_repeater_key)) return;

    NewElementWizardState& wizard = new_element_wizard_;
    wizard.repeater_add_begin = true;
    wizard.repeater_add_end = false;
    update_repeater_wizard_field_enablement(wizard);

    const MapElementEditFieldState* source_distance =
        find_inspector_field(inspector_, "distance");
    MapElementEditFieldState* begin_distance =
        find_inspector_field(wizard.form, "distance");
    MapElementEditFieldState* end_distance =
        find_inspector_field(wizard.form, "endDistance");
    if (!source_distance || !begin_distance || !end_distance) {
        KME_ADD_LOG("[error]Repeater change-point wizard distance field is unavailable");
        wizard.open = false;
        wizard.return_inspector_request.reset();
        wizard.close_after_successful_apply = false;
        wizard.apply_then_open_created_element = false;
        return;
    }

    const auto set_initial_value = [](MapElementEditFieldState& field,
                                      const std::string& value) {
        field.original_value = value;
        set_edit_field_buffer(field, value);
    };
    const std::string begin_distance_value = edit_field_buffer_text(*source_distance);
    set_initial_value(*begin_distance, begin_distance_value);
    set_initial_value(*end_distance, begin_distance_value);

    for (MapElementEditFieldState& target_field : wizard.form.fields) {
        if (target_field.key == "distance" || target_field.key == "endDistance" ||
            is_repeater_structure_key_field(target_field)) {
            continue;
        }
        const MapElementEditFieldState* source_field =
            find_inspector_field(inspector_, target_field.key);
        if (source_field && !source_field->read_only) {
            set_initial_value(target_field, edit_field_buffer_text(*source_field));
        } else if (target_field.key == "repeaterKey") {
            set_initial_value(target_field, source_repeater_key);
        }
    }

    std::vector<std::string> structure_keys;
    for (const MapElementEditFieldState& field : inspector_.fields) {
        if (is_repeater_structure_key_field(field) && !field.read_only) {
            structure_keys.push_back(edit_field_buffer_text(field));
        }
    }
    if (!structure_keys.empty()) {
        wizard.form.repeater_structure_keys_original = structure_keys;
        replace_repeater_structure_key_fields(wizard.form, structure_keys);
    }
}

void App::finish_new_element_wizard_after_successful_apply() {
    NewElementWizardState& wizard = new_element_wizard_;
    if (!wizard.close_after_successful_apply) return;
    wizard.open = false;
    wizard.return_inspector_request.reset();
    wizard.close_after_successful_apply = false;
    wizard.apply_then_open_created_element = false;
}

void App::apply_new_element_wizard_distance_prefill() {
    NewElementWizardState& wizard = new_element_wizard_;
    if (!wizard.distance_prefill) return;
    MapElementEditFieldState* distance = find_inspector_field(wizard.form, "distance");
    if (!distance) return;

    const std::string value = format_double(*wizard.distance_prefill, 0);
    distance->original_value = value;
    set_edit_field_buffer(*distance, value);
    if (MapElementEditFieldState* transition_start =
            find_inspector_field(wizard.form, "transitionStart")) {
        transition_start->original_value = value;
        set_edit_field_buffer(*transition_start, value);
    }
    if (MapElementEditFieldState* end_distance =
            find_inspector_field(wizard.form, "endDistance")) {
        end_distance->original_value = value;
        set_edit_field_buffer(*end_distance, value);
    }
    if (MapElementEditFieldState* end_transition_start =
            find_inspector_field(wizard.form, "endTransitionStart")) {
        end_transition_start->original_value = value;
        set_edit_field_buffer(*end_transition_start, value);
    }
}

void App::rebuild_new_element_wizard_form() {
    const std::vector<NewElementTemplate>& templates = new_element_templates();
    NewElementWizardState& wizard = new_element_wizard_;
    if (wizard.selected_template < 0 ||
        wizard.selected_template >= static_cast<int>(templates.size())) {
        wizard.selected_template = 0;
    }
    const NewElementTemplate& tpl = templates[static_cast<size_t>(wizard.selected_template)];
    const bool retargeting_current_form =
        wizard.built_template == wizard.selected_template &&
        wizard.built_target_file != wizard.target_file_path &&
        wizard.form.open &&
        std::string_view(wizard.form.row_kind) == tpl.row_kind;
    if (retargeting_current_form) {
        // The target controls where the insert is written, while the form owns
        // both ordinary fields and variable-length draft lists.
        wizard.form.source_file = wizard.target_file_path;
        wizard.form.source_file_name = display_name_from_path(wizard.form.source_file);
        wizard.built_target_file = wizard.target_file_path;
        return;
    }
    if (wizard.built_template != wizard.selected_template &&
        (tpl.row_kind == "curve" || tpl.row_kind == "gradient")) {
        wizard.own_track_add_start = true;
        wizard.own_track_add_end = true;
        wizard.own_track_start_add_transition = true;
        wizard.own_track_end_add_transition = true;
        wizard.own_track_curve_add_cant = true;
    }
    if (tpl.row_kind == "repeater" &&
        wizard.built_template != wizard.selected_template) {
        wizard.repeater_add_begin = true;
        wizard.repeater_add_end = true;
        wizard.confirm_repeater_change_point_once = false;
    }
    MapElementInspectorState form;
    form.open = true;
    form.row_kind = std::string(tpl.row_kind);
    form.title = tr("dialog.new_element_wizard");
    form.source_file = wizard.target_file_path;
    form.source_file_name = display_name_from_path(form.source_file);
    for (const NewElementFieldSpec& spec : tpl.fields) {
        MapElementEditFieldState field;
        field.key = spec.key;
        field.backend_key = spec.key;
        field.label = spec.label;
        field.numeric_constraint = spec.constraint;
        field.key_source = map_element_key_source_for_field(form.row_kind, spec.key);
        field.required = spec.required;
        field.optional_insertion_argument = spec.optional_insertion_argument;
        field.original_value =
            (spec.key == std::string_view("distance") ||
             spec.key == std::string_view("transitionStart") ||
             spec.key == std::string_view("endDistance") ||
             spec.key == std::string_view("endTransitionStart")) &&
                wizard.distance_prefill
            ? format_double(*wizard.distance_prefill, 0)
            : std::string(spec.default_value);
        set_edit_field_buffer(field, field.original_value);
        form.fields.push_back(std::move(field));
    }
    if (tpl.section_values) {
        form.fields.push_back(make_section_values_field(form, 0, "0"));
    }
    const bool coordinate_offset_form =
        tpl.row_kind == "structure.put" || tpl.row_kind == "repeater";
    if (coordinate_offset_form) {
        // The selected template fixes the long/zero-offset source shape, so the
        // wizard reuses the Inspector's field filtering without its conversion UI.
        form.coordinate_offsets_enabled = tpl.method == "Put" || tpl.method == "Begin";
    }
    if (tpl.row_kind == "repeater") {
        form.fields.push_back(make_repeater_structure_key_field(form, 0, {}));
    }
    wizard.form = std::move(form);
    if (tpl.row_kind == "repeater") {
        MapElementEditFieldState* begin_distance =
            find_inspector_field(wizard.form, "distance");
        MapElementEditFieldState* end_distance =
            find_inspector_field(wizard.form, "endDistance");
        if (begin_distance && end_distance) {
            end_distance->original_value = begin_distance->original_value;
            set_edit_field_buffer(*end_distance, edit_field_buffer_text(*begin_distance));
        }
        update_repeater_wizard_field_enablement(wizard);
    }
    update_own_track_wizard_field_enablement(wizard);
    wizard.built_template = wizard.selected_template;
    wizard.built_target_file = wizard.target_file_path;
}

bool App::apply_new_element_insert() {
    if (!edit_actions_available()) return false;
    const std::vector<NewElementTemplate>& templates = new_element_templates();
    NewElementWizardState& wizard = new_element_wizard_;
    if (wizard.selected_template < 0 ||
        wizard.selected_template >= static_cast<int>(templates.size())) {
        return false;
    }
    const NewElementTemplate& tpl = templates[static_cast<size_t>(wizard.selected_template)];
    MapElementInspectorState& form = wizard.form;
    if (wizard.built_template != wizard.selected_template ||
        wizard.built_target_file != wizard.target_file_path ||
        std::string_view(form.row_kind) != tpl.row_kind) {
        rebuild_new_element_wizard_form();
    }
    normalize_optional_insertion_argument_enablement(form);
    const bool own_track_wizard = is_own_track_wizard(wizard);
    update_own_track_wizard_field_enablement(wizard);
    if (own_track_wizard &&
        !wizard.own_track_add_start && !wizard.own_track_add_end) {
        set_program_status("status.edit.required_field");
        return false;
    }
    if (tpl.row_kind == "repeater") {
        if (!wizard.repeater_add_begin && !wizard.repeater_add_end) {
            set_program_status("status.edit.required_field");
            return false;
        }
        update_repeater_wizard_field_enablement(wizard);
    }

    size_t section_value_count = 0;
    size_t repeater_structure_key_count = 0;
    for (MapElementEditFieldState& field : form.fields) {
        if (field.disabled) continue;
        if (is_section_values_field(field)) {
            const std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
            if (value.empty()) {
                set_program_status("status.edit.required_field");
                return false;
            }
            double parsed_value = 0.0;
            if (!parse_gui_edit_number(value, &parsed_value)) {
                set_program_status("status.edit.invalid_number");
                return false;
            }
            ++section_value_count;
            continue;
        }
        const std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
        if (field.required && value.empty()) {
            set_program_status("status.edit.required_field");
            return false;
        }
        if (!validate_and_canonicalize_edit_field(field, true)) {
            set_program_status("status.edit.invalid_number");
            return false;
        }
        if (is_repeater_structure_key_field(field)) {
            ++repeater_structure_key_count;
        }
    }
    if (tpl.section_values && section_value_count == 0) {
        set_program_status("status.edit.required_field");
        return false;
    }
    if (tpl.row_kind == "repeater" && wizard.repeater_add_begin &&
        repeater_structure_key_count == 0) {
        set_program_status("status.edit.required_field");
        return false;
    }
    auto field_blank = [&](const char* key) {
        const MapElementEditFieldState* field = find_inspector_field(form, key);
        return !field || trim_gui_ascii_copy(edit_field_buffer_text(*field)).empty();
    };
    if (tpl.row_kind == "adhesion.change" &&
        field_blank("b") != field_blank("c")) {
        set_program_status("status.edit.required_field");
        return false;
    }
    if (tpl.row_kind == "fog.change") {
        const bool density = !field_blank("density");
        const bool red = !field_blank("red");
        const bool green = !field_blank("green");
        const bool blue = !field_blank("blue");
        const bool all_colors = red && green && blue;
        const bool any_color = red || green || blue;
        const std::string method(tpl.method);
        if (method == "Set") {
            if (!density || !all_colors) {
                set_program_status("status.edit.required_field");
                return false;
            }
        } else if (any_color != all_colors || (all_colors && !density)) {
            set_program_status("status.edit.required_field");
            return false;
        }
    }
    if (own_track_wizard) {
        const auto transition_after_statement = [&](const char* transition_key,
                                                     const char* distance_key) {
            const MapElementEditFieldState* transition =
                find_inspector_field(form, transition_key);
            const MapElementEditFieldState* distance =
                find_inspector_field(form, distance_key);
            double transition_value = 0.0;
            double distance_value = 0.0;
            return !transition || !distance ||
                !parse_gui_edit_number(edit_field_buffer_text(*transition),
                                       &transition_value) ||
                !parse_gui_edit_number(edit_field_buffer_text(*distance),
                                       &distance_value) ||
                transition_value > distance_value;
        };
        if ((wizard.own_track_add_start &&
             wizard.own_track_start_add_transition &&
             transition_after_statement("transitionStart", "distance")) ||
            (wizard.own_track_add_end &&
             wizard.own_track_end_add_transition &&
             transition_after_statement("endTransitionStart", "endDistance"))) {
            set_program_status("status.edit.transition_start_after_distance");
            return false;
        }
    }

    double repeater_begin_distance = 0.0;
    double repeater_end_distance = 0.0;
    std::string repeater_key;
    if (tpl.row_kind == "repeater") {
        const MapElementEditFieldState* key_field =
            find_inspector_field(form, "repeaterKey");
        repeater_key = key_field
            ? trim_gui_ascii_copy(edit_field_buffer_text(*key_field))
            : std::string{};
        if (repeater_key.empty()) {
            set_program_status("status.edit.required_field");
            return false;
        }
        if (wizard.repeater_add_begin) {
            const MapElementEditFieldState* distance_field =
                find_inspector_field(form, "distance");
            if (!distance_field || !parse_gui_edit_number(
                    edit_field_buffer_text(*distance_field), &repeater_begin_distance)) {
                set_program_status("status.edit.invalid_number");
                return false;
            }
        }
        if (wizard.repeater_add_end) {
            const MapElementEditFieldState* end_distance_field =
                find_inspector_field(form, "endDistance");
            if (!end_distance_field || !parse_gui_edit_number(
                    edit_field_buffer_text(*end_distance_field), &repeater_end_distance)) {
                set_program_status("status.edit.invalid_number");
                return false;
            }
        }
        if (wizard.repeater_add_begin && wizard.repeater_add_end &&
            repeater_end_distance < repeater_begin_distance) {
            set_program_status("status.edit.repeater_end_before_begin");
            return false;
        }

        const repeater_linkage::Linkage linkage =
            repeater_linkage::pair_linkage(table_repeater_events(model_.repeaters));
        if (wizard.repeater_add_end && repeater_linkage::chain_at_distance(
                linkage, repeater_key, repeater_end_distance, true)) {
            wizard.repeater_extra_end_prompt_requested = true;
            return false;
        }
        if (wizard.repeater_add_begin && !wizard.repeater_add_end &&
            repeater_linkage::chain_at_distance(
                linkage, repeater_key, repeater_begin_distance) &&
            !wizard.confirm_repeater_change_point_once) {
            wizard.repeater_change_point_prompt_requested = true;
            return false;
        }
    }

    if (!find_model_source_file(model_, wizard.target_file_path)) {
        KME_ADD_LOG("[warn]insert target file is not part of the loaded map: " +
                wizard.target_file_path);
        return false;
    }
    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    std::string insert_base;
    std::string primary_insert_id;
    const auto insert_id_exists = [&](const std::string& base) {
        if (candidate.find(base) != candidate.end()) return true;
        const std::string prefix = base + "-";
        return std::any_of(candidate.begin(), candidate.end(), [&](const auto& entry) {
            return entry.first.compare(0, prefix.size(), prefix) == 0;
        });
    };
    do {
        insert_base = "insert-" + std::to_string(++wizard.insert_sequence);
    } while (insert_id_exists(insert_base));
    auto make_insert_change = [&](const std::string& edit_id) {
        MapElementPendingChange change;
        change.change_id = edit_id;
        change.edit_id = edit_id;
        change.row_kind = std::string(tpl.row_kind);
        change.operation = "insert";
        change.target_file_path = wizard.target_file_path;
        return change;
    };

    // An insert has no existing source row whose optimistic-concurrency hash
    // must be preserved. Leave expectedSourceHash empty so maploader compares
    // the physical file against the current handle's authoritative disk
    // baseline. The GUI model may represent a dirty working-copy hash and can
    // otherwise become stale across an Apply -> Save -> Apply cycle.
    if (tpl.row_kind == "repeater") {
        const std::string pair_id = wizard.repeater_add_begin && wizard.repeater_add_end
            ? "repeater-pair-" + std::to_string(wizard.insert_sequence)
            : std::string{};
        if (wizard.repeater_add_begin) {
            MapElementPendingChange begin = make_insert_change(insert_base + "-begin");
            begin.repeater_pair_id = pair_id;
            begin.confirm_repeater_change_point =
                wizard.confirm_repeater_change_point_once;
            for (const MapElementEditFieldState& field : form.fields) {
                if (field.read_only || field.disabled || field.key == "endDistance") continue;
                const std::string& backend_key =
                    field.backend_key.empty() ? field.key : field.backend_key;
                begin.field_changes[backend_key] =
                    trim_gui_ascii_copy(edit_field_buffer_text(field));
            }
            begin.field_changes["method"] = std::string(tpl.method);
            begin.field_changes["structureKeys.count"] =
                std::to_string(repeater_structure_key_count);
            candidate[begin.edit_id] = std::move(begin);
        }
        if (wizard.repeater_add_end) {
            MapElementPendingChange end = make_insert_change(insert_base + "-end");
            end.repeater_pair_id = pair_id;
            end.field_changes["distance"] =
                format_double(repeater_end_distance, 12);
            end.field_changes["repeaterKey"] = repeater_key;
            end.field_changes["method"] = "End";
            candidate[end.edit_id] = std::move(end);
        }
        primary_insert_id = wizard.repeater_add_begin
            ? insert_base + "-begin"
            : insert_base + "-end";
        wizard.confirm_repeater_change_point_once = false;
    } else if (own_track_wizard) {
        const auto value = [&](const char* key) {
            const MapElementEditFieldState* field =
                find_inspector_field(form, key);
            return field
                ? trim_gui_ascii_copy(edit_field_buffer_text(*field))
                : std::string{};
        };
        const std::string family = tpl.row_kind == "curve" ? "Curve." : "Gradient.";
        const auto add_transition = [&](const std::string& edit_id,
                                        const char* distance_key) {
            MapElementPendingChange transition = make_insert_change(edit_id);
            transition.field_changes["distance"] = value(distance_key);
            transition.field_changes["method"] = family + "BeginTransition";
            candidate[transition.edit_id] = std::move(transition);
        };
        if (wizard.own_track_add_start) {
            if (wizard.own_track_start_add_transition) {
                add_transition(insert_base + "-a-start-transition", "transitionStart");
            }
            MapElementPendingChange start =
                make_insert_change(insert_base + "-b-start");
            start.field_changes["distance"] = value("distance");
            if (tpl.row_kind == "curve") {
                start.field_changes["method"] = family + value("method");
                start.field_changes["radius"] = value("radius");
                if (wizard.own_track_curve_add_cant) {
                    start.field_changes["cant"] = value("cant");
                }
            } else {
                start.field_changes["method"] = "Gradient.Begin";
                start.field_changes["gradient"] = value("gradient");
            }
            candidate[start.edit_id] = std::move(start);
        }
        if (wizard.own_track_add_end) {
            if (wizard.own_track_end_add_transition) {
                add_transition(insert_base + "-c-end-transition", "endTransitionStart");
            }
            MapElementPendingChange end =
                make_insert_change(insert_base + "-d-end");
            end.field_changes["distance"] = value("endDistance");
            end.field_changes["method"] = family + "End";
            candidate[end.edit_id] = std::move(end);
        }
        primary_insert_id = wizard.own_track_add_start
            ? insert_base + "-b-start"
            : insert_base + "-d-end";
    } else {
        MapElementPendingChange change = make_insert_change(insert_base);
        for (const MapElementEditFieldState& field : form.fields) {
            if (field.read_only || field.disabled || is_section_values_field(field)) continue;
            const std::string& backend_key =
                field.backend_key.empty() ? field.key : field.backend_key;
            change.field_changes[backend_key] =
                trim_gui_ascii_copy(edit_field_buffer_text(field));
        }
        if (!tpl.method.empty()) {
            change.field_changes["method"] = std::string(tpl.method);
        }
        if (tpl.section_values) {
            change.field_changes["values.count"] = std::to_string(section_value_count);
            size_t value_index = 0;
            for (const MapElementEditFieldState& field : form.fields) {
                if (!is_section_values_field(field)) continue;
                change.field_changes["values." + std::to_string(value_index++)] =
                    trim_gui_ascii_copy(edit_field_buffer_text(field));
            }
        }
        candidate[change.edit_id] = std::move(change);
        primary_insert_id = insert_base;
    }

    std::optional<MapElementInspectorRequest> created_element_request;
    if (wizard.apply_then_open_created_element) {
        created_element_request = MapElementInspectorRequest{
            primary_insert_id, std::string(tpl.row_kind)};
        wizard.return_inspector_request = *created_element_request;
        wizard.close_after_successful_apply = true;
    }
    const std::optional<MapElementInspectorRequest> reload_request =
        created_element_request
        ? created_element_request
        : wizard.return_inspector_request;
    if (!apply_edit_ledger_to_preview(candidate, reload_request, false)) {
        if (distance_resolution_workflow_.phase == DistanceResolutionPhase::None &&
            !distance_resolution_workflow_.retry_requested) {
            set_program_status("status.edit.pending");
        }
        return false;
    }
    finish_new_element_wizard_after_successful_apply();
    set_program_status("status.edit.applied_to_preview");
    return true;
}

void App::render_new_element_wizard() {
    if (!new_element_wizard_.open) return;
    if (!edit_actions_available()) {
        new_element_wizard_.open = false;
        return;
    }
    NewElementWizardState& wizard = new_element_wizard_;
    if (!wizard.target_candidates_built) {
        wizard.target_file_candidates = new_element_target_candidates(model_);
        wizard.target_candidates_built = true;
        if (wizard.target_file_path.empty() ||
            std::find(wizard.target_file_candidates.begin(),
                      wizard.target_file_candidates.end(),
                      wizard.target_file_path) == wizard.target_file_candidates.end()) {
            wizard.target_file_path = wizard.target_file_candidates.empty()
                ? std::string{}
                : wizard.target_file_candidates.front();
        }
    }
    const std::vector<NewElementTemplate>& templates = new_element_templates();
    const std::vector<size_t>& template_display_order =
        new_element_template_display_order();

    const std::string title = tr("dialog.new_element_wizard") + "###NewElementWizard";
    ImGui::SetNextWindowSize(ImVec2(980.0f, 660.0f), ImGuiCond_Always);
    const bool operation_pending = edit_ui_operation_pending();
    bool* wizard_open = operation_pending ? nullptr : &wizard.open;
    ImGuiWindowFlags wizard_flags = ImGuiWindowFlags_NoResize;
    if (operation_pending) wizard_flags |= ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin(title.c_str(), wizard_open, wizard_flags)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##NewElementTemplateList", ImVec2(360.0f, -ImGui::GetFrameHeightWithSpacing()),
                      true);
    const ImVec4 dim_text = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const auto render_template = [&](size_t index) {
        const NewElementTemplate& tpl = templates[index];
        const int template_index = static_cast<int>(index);
        const std::string& usage = tr(tpl.usage_key);
        const std::string template_label = std::string(tpl.syntax) +
            "###NewElementTemplate_" + tpl.id;
        if (ImGui::Selectable(template_label.c_str(),
                              wizard.selected_template == template_index)) {
            wizard.selected_template = template_index;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", usage.c_str());
        }
        ImGui::PushStyleColor(ImGuiCol_Text, dim_text);
        render_new_element_usage_text(usage);
        ImGui::PopStyleColor();
        ImGui::Separator();
    };
    for (const NewElementTemplateCategoryInfo& category :
         k_new_element_template_categories) {
        const std::string category_label = tr(category.label_key) +
            "###NewElementTemplateCategory_" + category.id;
        if (!ImGui::CollapsingHeader(category_label.c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        // CollapsingHeader does not indent its contents; the child padding is
        // the intended small inset for template labels and descriptions.
        for (size_t index : template_display_order) {
            if (templates[index].category != category.category) continue;
            render_template(index);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##NewElementForm", ImVec2(-1.0f, -ImGui::GetFrameHeightWithSpacing()), true);
    ImGui::TextUnformatted(tr("label.new_element_target_file").c_str());
    ImGui::SetNextItemWidth(-1.0f);
    const std::string target_preview = wizard.target_file_path.empty()
        ? tr("status.edit.no_distance_source")
        : display_name_from_path(wizard.target_file_path);
    ImGui::BeginDisabled(wizard.target_file_candidates.empty());
    if (ImGui::BeginCombo("##NewElementTargetFile", target_preview.c_str())) {
        for (const std::string& file_path : wizard.target_file_candidates) {
            const bool selected = file_path == wizard.target_file_path;
            if (ImGui::Selectable(display_name_from_path(file_path).c_str(), selected)) {
                wizard.target_file_path = file_path;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    const bool has_target = !wizard.target_file_candidates.empty() &&
        !wizard.target_file_path.empty();
    if (!has_target) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", tr("status.edit.no_distance_source").c_str());
    } else {
        if (wizard.built_template != wizard.selected_template ||
            wizard.built_target_file != wizard.target_file_path) {
            rebuild_new_element_wizard_form();
        }
        ImGui::Separator();
        const NewElementTemplate& tpl = templates[static_cast<size_t>(wizard.selected_template)];
        normalize_optional_insertion_argument_enablement(wizard.form);
        if (tpl.row_kind == "repeater") {
            ImGui::BeginDisabled(wizard.repeater_add_begin && !wizard.repeater_add_end);
            ImGui::Checkbox(tr("chk.new_element_repeater_add_begin").c_str(),
                            &wizard.repeater_add_begin);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(wizard.repeater_add_end && !wizard.repeater_add_begin);
            ImGui::Checkbox(tr("chk.new_element_repeater_add_end").c_str(),
                            &wizard.repeater_add_end);
            ImGui::EndDisabled();
            update_repeater_wizard_field_enablement(wizard);
            render_map_element_field_inputs(wizard.form);
            ImGui::BeginDisabled(!wizard.repeater_add_begin);
            render_repeater_structure_keys_edit_ui(wizard.form);
            ImGui::EndDisabled();
        } else if (tpl.section_values) {
            render_map_element_field_inputs(wizard.form);
            render_section_values_edit_ui(wizard.form);
        } else if (is_own_track_wizard(wizard)) {
            const bool curve = tpl.row_kind == "curve";
            const auto render_field = [&](const char* key) {
                MapElementEditFieldState* field =
                    find_inspector_field(wizard.form, key);
                if (!field) return false;
                const bool changed =
                    edit_field_buffer_text(*field) != field->original_value;
                if (changed) {
                    ImGui::PushStyleColor(
                        ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
                }
                const float input_width =
                    std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.55f);
                ImGui::BeginDisabled(field->read_only || field->disabled);
                bool input_changed = false;
                if (field->key == "method") {
                    ImGui::SetNextItemWidth(input_width);
                    const std::string current = edit_field_buffer_text(*field);
                    if (ImGui::BeginCombo(field->label.c_str(), current.c_str())) {
                        for (const char* option : {"Begin", "Change"}) {
                            const bool selected = current == option;
                            if (ImGui::Selectable(option, selected) && !selected) {
                                set_edit_field_buffer(*field, option);
                                input_changed = true;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    input_changed =
                        render_map_element_field_control(*field, input_width);
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemDeactivatedAfterEdit() &&
                    !validate_and_canonicalize_edit_field(*field, true)) {
                    set_program_status("status.edit.invalid_number");
                }
                if (changed) ImGui::PopStyleColor();
                return input_changed;
            };

            update_own_track_wizard_field_enablement(wizard);
            ImGui::BeginDisabled(
                wizard.own_track_add_start && !wizard.own_track_add_end);
            const bool start_changed = ImGui::Checkbox(
                tr("chk.new_element_add_start").c_str(),
                &wizard.own_track_add_start);
            ImGui::EndDisabled();
            if (start_changed && wizard.own_track_add_start &&
                wizard.own_track_add_end) {
                wizard.own_track_start_add_transition =
                    wizard.own_track_end_add_transition;
            }
            update_own_track_wizard_field_enablement(wizard);

            render_field("transitionStart");
            ImGui::SameLine();
            const bool curve_change =
                curve && own_track_curve_method(wizard) == "Change";
            ImGui::BeginDisabled(!wizard.own_track_add_start || curve_change);
            bool start_transition = wizard.own_track_start_add_transition;
            if (ImGui::Checkbox(
                    (tr("chk.new_element_own_track_add_transition") +
                     "##OwnTrackStartTransition").c_str(),
                    &start_transition)) {
                wizard.own_track_start_add_transition = start_transition;
                if (curve) wizard.own_track_curve_add_cant = start_transition;
                if (wizard.own_track_add_end) {
                    wizard.own_track_end_add_transition = start_transition;
                }
            }
            ImGui::EndDisabled();
            update_own_track_wizard_field_enablement(wizard);

            render_field("distance");
            if (curve) {
                if (render_field("method")) {
                    update_own_track_wizard_field_enablement(wizard);
                }
                render_field("radius");
                render_field("cant");
                ImGui::SameLine();
                const bool cant_available = wizard.own_track_add_start &&
                    own_track_curve_method(wizard) == "Begin" &&
                    wizard.own_track_start_add_transition;
                ImGui::BeginDisabled(!cant_available);
                bool add_cant = wizard.own_track_curve_add_cant;
                if (ImGui::Checkbox(
                        tr("chk.new_element_add_parameter").c_str(), &add_cant)) {
                    wizard.own_track_curve_add_cant = add_cant;
                    if (!add_cant) {
                        wizard.own_track_start_add_transition = false;
                        if (wizard.own_track_add_end) {
                            wizard.own_track_end_add_transition = false;
                        }
                    }
                }
                ImGui::EndDisabled();
                update_own_track_wizard_field_enablement(wizard);
            } else {
                render_field("gradient");
            }

            ImGui::Separator();
            ImGui::BeginDisabled(
                wizard.own_track_add_end && !wizard.own_track_add_start);
            const bool end_changed = ImGui::Checkbox(
                tr("chk.new_element_add_end").c_str(),
                &wizard.own_track_add_end);
            ImGui::EndDisabled();
            if (end_changed && wizard.own_track_add_end &&
                wizard.own_track_add_start) {
                wizard.own_track_end_add_transition =
                    wizard.own_track_start_add_transition;
            }
            update_own_track_wizard_field_enablement(wizard);

            render_field("endTransitionStart");
            ImGui::SameLine();
            const bool linked_curve_change = wizard.own_track_add_start &&
                curve && own_track_curve_method(wizard) == "Change";
            ImGui::BeginDisabled(!wizard.own_track_add_end || linked_curve_change);
            bool end_transition = wizard.own_track_end_add_transition;
            if (ImGui::Checkbox(
                    (tr("chk.new_element_own_track_add_transition") +
                     "##OwnTrackEndTransition").c_str(),
                    &end_transition)) {
                wizard.own_track_end_add_transition = end_transition;
                if (wizard.own_track_add_start) {
                    wizard.own_track_start_add_transition = end_transition;
                    if (curve) wizard.own_track_curve_add_cant = end_transition;
                }
            }
            ImGui::EndDisabled();
            update_own_track_wizard_field_enablement(wizard);
            render_field("endDistance");
        } else {
            render_map_element_field_inputs(
                wizard.form, tpl.row_kind == "otherTrack.change");
        }
        ImGui::Separator();
        if (ImGui::Button(tr("button.apply").c_str())) {
            if (wizard.apply_then_open_created_element) {
                wizard.apply_then_open_created_element = false;
                wizard.close_after_successful_apply = false;
                wizard.return_inspector_request.reset();
            }
            request_edit_ui_operation(PendingEditUiOperation::ApplyNewElement);
        }
        ImGui::SameLine();
        const bool jump_to_created_element_available =
            !wizard.return_inspector_request.has_value() ||
            wizard.apply_then_open_created_element;
        ImGui::BeginDisabled(!jump_to_created_element_available);
        if (ImGui::Button(tr("button.apply_jump_to_edit").c_str())) {
            wizard.apply_then_open_created_element = true;
            request_edit_ui_operation(PendingEditUiOperation::ApplyNewElement);
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("button.close").c_str())) {
        if (wizard.apply_then_open_created_element) {
            wizard.apply_then_open_created_element = false;
            wizard.close_after_successful_apply = false;
            wizard.return_inspector_request.reset();
        }
        cancel_distance_resolution_workflow();
        wizard.open = false;
    }
    ImGui::EndChild();
    ImGui::End();
}

void App::open_new_file_wizard(std::optional<NewFileKind> template_kind) {
    new_file_wizard_ = NewFileWizardState{};
    if (template_kind) {
        const auto it = std::find_if(
            k_new_file_templates.begin(), k_new_file_templates.end(),
            [&](const NewFileTemplate& tpl) { return tpl.kind == *template_kind; });
        if (it != k_new_file_templates.end()) {
            new_file_wizard_.selected_template = static_cast<int>(
                std::distance(k_new_file_templates.begin(), it));
        }
    }
    new_file_wizard_.open = true;
}

void App::render_new_file_wizard() {
    if (!new_file_wizard_.open) return;
    NewFileWizardState& wizard = new_file_wizard_;
    if (!wizard.target_candidates_built) {
        if (has_model_) {
            wizard.target_file_candidates = new_file_reference_target_candidates(model_);
        }
        wizard.target_candidates_built = true;
        wizard.target_file_path = wizard.target_file_candidates.empty()
            ? std::string{}
            : wizard.target_file_candidates.front();
    }
    if (wizard.selected_template < 0 ||
        wizard.selected_template >= static_cast<int>(k_new_file_templates.size())) {
        wizard.selected_template = 0;
    }

    const std::string title = tr("dialog.new_file_wizard") + "###NewFileWizard";
    ImGui::SetNextWindowSize(ImVec2(980.0f, 660.0f), ImGuiCond_Always);
    const bool operation_pending = edit_ui_operation_pending();
    bool* wizard_open = operation_pending ? nullptr : &wizard.open;
    ImGuiWindowFlags wizard_flags = ImGuiWindowFlags_NoResize;
    if (operation_pending) wizard_flags |= ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin(title.c_str(), wizard_open, wizard_flags)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##NewFileTemplateList", ImVec2(360.0f, -ImGui::GetFrameHeightWithSpacing()),
                      true);
    const ImVec4 dim_text = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    for (const NewFileTemplateCategoryInfo& category : k_new_file_template_categories) {
        const std::string category_label = tr(category.label_key) +
            "###NewFileTemplateCategory_" + category.id;
        if (!ImGui::CollapsingHeader(category_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        for (size_t index = 0; index < k_new_file_templates.size(); ++index) {
            const NewFileTemplate& tpl = k_new_file_templates[index];
            if (std::string_view(tpl.category_id) != category.id) continue;
            const std::string label = std::string(tpl.label) + "###NewFileTemplate_" +
                std::to_string(index);
            if (ImGui::Selectable(label.c_str(), wizard.selected_template == static_cast<int>(index))) {
                wizard.selected_template = static_cast<int>(index);
            }
            const std::string& usage = tr(tpl.usage_key);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", usage.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, dim_text);
            render_new_element_usage_text(usage);
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##NewFileForm", ImVec2(-1.0f, -ImGui::GetFrameHeightWithSpacing()), true);
    const NewFileTemplate& tpl =
        k_new_file_templates[static_cast<size_t>(wizard.selected_template)];
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::optional<ResourceListKind> resource_list_kind =
        resource_list_kind_for_new_file(tpl.kind);
    const bool resource_list_already_referenced =
        resource_list_kind && has_model_ &&
        new_file_resource_list_is_already_referenced(
            model_, pending_edit_changes_, tpl.kind);
    if (resource_list_kind && ImGui::Button(tr("button.import_file").c_str())) {
        const std::string initial_directory = !wizard.directory.empty()
            ? wizard.directory
            : has_model_
                ? list_asset_picker_initial_directory({}, model_.path)
                : std::string{};
        const std::string selected_file = open_include_file_dialog(
            initial_directory, "dialog.select_resource_list_file",
            "dialog.filter.resource_list_files");
        if (!selected_file.empty()) {
            const std::filesystem::path imported_path(utf8_to_wide(selected_file));
            std::string extension = wide_to_utf8(imported_path.extension().wstring());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            if (extension != ".txt" && extension != ".csv") {
                set_program_status("status.new_file.invalid_path");
            } else {
                wizard.file_name = wide_to_utf8(imported_path.stem().wstring());
                wizard.directory = wide_to_utf8(imported_path.parent_path().wstring());
                wizard.use_csv_extension = extension == ".csv";
            }
        }
    }
    ImGui::TextUnformatted(tr("label.new_file_name").c_str());
    const float extension_control_width = ImGui::GetFrameHeight() * 2.0f +
        ImGui::CalcTextSize(".txt").x + ImGui::CalcTextSize(".csv").x +
        style.ItemSpacing.x * 2.0f + style.ItemInnerSpacing.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x -
                                     extension_control_width));
    ImGui::InputText("##NewFileName", &wizard.file_name);
    ImGui::SameLine();
    if (ImGui::RadioButton(".txt##NewFileTextExtension", !wizard.use_csv_extension)) {
        wizard.use_csv_extension = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(".csv##NewFileCsvExtension", wizard.use_csv_extension)) {
        wizard.use_csv_extension = true;
    }

    ImGui::TextUnformatted(tr("label.new_file_directory").c_str());
    const std::string select_directory = tr("button.select_directory");
    const float select_directory_width = ImGui::CalcTextSize(select_directory.c_str()).x +
        style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x -
                                     select_directory_width - style.ItemSpacing.x));
    ImGui::InputText("##NewFileDirectory", &wizard.directory);
    ImGui::SameLine();
    if (ImGui::Button(select_directory.c_str())) {
        const std::string selected = choose_folder_dialog("dialog.select_new_file_directory");
        if (!selected.empty()) wizard.directory = selected;
    }

    ImGui::TextUnformatted(tr("label.new_file_reference").c_str());
    const std::string target_preview = wizard.target_file_path.empty()
        ? tr("status.new_file.no_reference_target")
        : display_name_from_path(wizard.target_file_path);
    if (wizard.target_file_candidates.empty()) {
        ImGui::TextWrapped("%s", tr("status.new_file.no_reference_target").c_str());
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(wizard.target_file_candidates.empty() ||
                         resource_list_already_referenced);
    if (ImGui::BeginCombo("##NewFileTargetFile", target_preview.c_str())) {
        for (const std::string& file_path : wizard.target_file_candidates) {
            const bool selected = file_path == wizard.target_file_path;
            if (ImGui::Selectable(display_name_from_path(file_path).c_str(), selected)) {
                wizard.target_file_path = file_path;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    const bool reference_requested = !wizard.target_file_path.empty();
    if (reference_requested && !edit_actions_available()) {
        ImGui::TextWrapped("%s", tr("status.new_file.reference_requires_edit").c_str());
    }
    ImGui::Separator();
    const bool create_disabled =
        (reference_requested && !edit_actions_available()) || resource_list_already_referenced;
    ImGui::BeginDisabled(create_disabled);
    if (ImGui::Button(tr("button.confirm").c_str())) {
        request_new_file_create(NewFileCreateRequest{
            tpl.kind, wizard.file_name, wizard.directory, wizard.target_file_path,
            wizard.use_csv_extension});
    }
    ImGui::EndDisabled();
    if (tpl.kind == NewFileKind::Map) {
        ImGui::SameLine();
        ImGui::BeginDisabled(create_disabled || load_state_.running);
        if (ImGui::Button(tr("button.confirm_and_load").c_str())) {
            request_new_file_create(NewFileCreateRequest{
                tpl.kind, wizard.file_name, wizard.directory, wizard.target_file_path,
                wizard.use_csv_extension, true});
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("button.close").c_str())) wizard.open = false;
    if (resource_list_already_referenced) {
        std::string message = tr("status.new_file.resource_list_already_present");
        constexpr std::string_view placeholder = "{resource_list}";
        const size_t placeholder_position = message.find(placeholder);
        if (placeholder_position != std::string::npos) {
            message.replace(placeholder_position, placeholder.size(),
                            tr(resource_list_name_translation_key(*resource_list_kind)));
        }
        ImGui::TextWrapped("%s", message.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}
