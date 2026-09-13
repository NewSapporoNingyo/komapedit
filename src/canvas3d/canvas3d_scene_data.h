/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3d_types.h"

namespace canvas3d_detail {

void populate_canvas3d_scene_route_values(Canvas3DSceneRouteInfo& route_info,
                                          const MapModel& model);
void populate_canvas3d_scene_route_stations(Canvas3DSceneRouteInfo& route_info,
                                            const MapModel& model);
std::optional<size_t> canvas3d_scene_signal_speed_index(
    const std::string& value);
void populate_canvas3d_scene_speed_limits(Canvas3DSceneRouteInfo& route_info,
                                          const MapModel& model);
void populate_canvas3d_scene_section_signals(Canvas3DSceneRouteInfo& route_info,
                                             const MapModel& model);
void sort_canvas3d_scene_markers(std::vector<Canvas3DSceneMarker>& markers);
void populate_canvas3d_scene_markers(Canvas3DScene& scene, const MapModel& model);
void populate_canvas3d_scene_fog(Canvas3DScene& scene, const MapModel& model);
void populate_canvas3d_scene_draw_distances(Canvas3DScene& scene, const MapModel& model);
SceneFogSample sample_canvas3d_scene_fog(
    const std::vector<Canvas3DSceneFogKeyframe>& keyframes,
    double distance,
    bool enabled);
bool populate_canvas3d_scene_dynamic_content(Canvas3DScene& scene,
                                             const MapModel& model,
                                             int station_index);

} // namespace canvas3d_detail
