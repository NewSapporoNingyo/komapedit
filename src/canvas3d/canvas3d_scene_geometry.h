/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3d_types.h"

namespace canvas3d_detail {

bool scene_repeater_has_interval(const Canvas3DRepeaterSegment& repeater);
bool scene_repeater_index_range(const Canvas3DRepeaterSegment& repeater,
                                double range_min,
                                double range_max,
                                SceneRepeaterIndexRange& out);
size_t scene_repeater_instance_count(const Canvas3DRepeaterSegment& repeater);
bool scene_repeater_last_instance(const Canvas3DRepeaterSegment& repeater,
                                  double& distance,
                                  size_t& model_index);
bool scene_repeater_render_distance_span(const Canvas3DRepeaterSegment& repeater,
                                         double& first_distance,
                                         double& last_distance);
std::string scene_model_key(std::string key);
std::optional<Canvas3DTrackPoint> scene_sample_track_path_points(const Canvas3DTrackPath& path,
                                                                 double distance);
const Canvas3DTrackPath* scene_own_track_path(const Canvas3DScene& scene);
const Canvas3DTrackPath* scene_other_track_path_for_key(const Canvas3DScene& scene,
                                                        const std::string& normalized_key);
const Canvas3DTrackPath* scene_placement_track_path_for_key(const Canvas3DScene& scene,
                                                            const std::string& key);
std::string scene_model_key_for_instance(const Canvas3DModelInstance& instance,
                                         size_t geometry_generation);
std::string scene_put_between_preview_model_key(const std::string& edit_id,
                                                size_t geometry_generation);
template <typename Visitor>
void visit_scene_repeater_indices(const SceneRepeaterIndexRange& range, Visitor visit) {
    if (range.last < range.first) return;
    for (long long index = range.first;; ++index) {
        if (!visit(index) || index == range.last) break;
    }
}

} // namespace canvas3d_detail
