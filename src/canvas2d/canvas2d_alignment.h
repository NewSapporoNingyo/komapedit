/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "../main_window/kme.h"
#include "own_track_geometry.h"

#include <condition_variable>

namespace canvas2d {

struct PlanScreenTransform;

enum class AlignmentDragSurface { Plan, Profile };

struct AlignmentDraft {
    std::optional<double> distance;
    std::optional<double> radius;
};

struct AlignmentTarget {
    std::string_view row_kind;
    std::size_t row_index;
    double distance;
    std::optional<double> radius;
    int source_order;
};

struct AlignmentSpan {
    double start;
    double end;
    double radius;
    bool transition;
    std::string edit_id;
    std::string label;
};

struct AlignmentRadiusLabel {
    const AlignmentSpan* span;
    ImVec2 anchor;
};

struct AlignmentOverlay {
    std::vector<OwnTrackEditMarker> curve_markers;
    std::vector<OwnTrackEditMarker> gradient_markers;
    std::vector<PlanCurveInterpolateMarker> interpolate_markers;
};

class AlignmentEditor {
public:
    AlignmentEditor() = default;
    ~AlignmentEditor();

    bool dragging() const { return drag_.has_value(); }
    bool dragging(AlignmentDragSurface surface) const {
        return drag_ && drag_->surface == surface;
    }
    bool dirty() const { return !drafts_.empty(); }
    bool current_preview_ready() const;
    const auto& drafts() const { return drafts_; }
    const AlignmentTarget* target(const std::string& edit_id) const;

    void set_source(const MapModel& model, std::uint64_t revision,
                    const std::shared_ptr<const std::vector<RepeaterOverlayRow>>& repeaters);
    std::string poll();
    void clear_drafts();
    void cancel_drag();
    void set_distance(const std::string& edit_id, double distance);
    void step_radius(const std::string& edit_id, int ticks);

    const Matrix& geometry(const Matrix& applied) const;
    std::uint64_t preview_revision() const;
    std::uint64_t marker_revision() const { return marker_revision_; }
    const AlignmentReprojection* marker_projection() const { return marker_projection_.get(); }
    bool has_source(std::uint64_t revision) const { return base_ && source_revision_ == revision; }
    double preview_distance(const std::string& id, double applied) const;
    const std::vector<AlignmentSpan>& spans() const;
    const std::vector<AlignmentRadiusLabel>& radius_labels(
        const Matrix& track, const PlanScreenTransform& transform,
        double distance_min, double distance_max, ImVec2 origin, ImVec2 size);
    const AlignmentOverlay* overlay(const PlanData& plan, std::uint64_t plan_generation,
                                    const PlanScreenTransform& transform);
    bool process_mouse(const PlanData& plan, const AlignmentOverlay* overlay,
                       const Matrix& geometry, const PlanScreenTransform& transform,
                       ImVec2 mouse, double distance_min, double distance_max,
                       float marker_scale, bool can_start, bool show_curves,
                       PlanMarkerSelection& selection);
    bool start_profile_drag(const std::string& id, double mouse_x, double metres_per_pixel,
                            double distance_min, double distance_max);
    void process_profile_drag(double mouse_x);
    void draw_drag_label(AlignmentDragSurface surface, ImDrawList* draw, ImVec2 mouse,
                         float marker_scale) const;

    struct Metrics {
        std::size_t submitted = 0;
        std::size_t computed = 0;
        std::size_t superseded = 0;
        std::size_t reused_rows = 0;
        std::vector<double> latency_ms;
    };
    const Metrics& metrics() const { return metrics_; }
    void measure_preview() { measure_preview_ = true; }

private:
    struct CurveSpans {
        std::vector<AlignmentSpan> items;
        std::vector<std::size_t> label_order;
    };
    struct SourcedEvent {
        own_track_geometry::Event value;
        int source_order;
        std::string edit_id;
        bool editable_radius;
    };
    struct Base {
        std::vector<double> distances;
        std::vector<SourcedEvent> events;
        CurveSpans spans;
        std::array<double, 3> distribution;
        std::vector<Matrix> other_tracks;
        std::shared_ptr<const std::vector<RepeaterOverlayRow>> repeaters;
    };
    struct Request {
        std::shared_ptr<const Base> base;
        std::map<std::string, AlignmentDraft> drafts;
        std::uint64_t revision;
        std::chrono::steady_clock::time_point started;
        std::optional<std::chrono::steady_clock::time_point> refresh_markers_at;
    };
    struct Preview {
        std::shared_ptr<const Matrix> geometry;
        CurveSpans spans;
        std::map<std::string, AlignmentDraft> drafts;
        std::size_t reused_rows = 0;
    };
    struct Result {
        std::shared_ptr<const Preview> preview;
        std::shared_ptr<const AlignmentReprojection> marker_projection;
        std::shared_ptr<const Base> base;
        std::uint64_t revision = 0;
        std::chrono::steady_clock::time_point started;
        std::string error;
    };
    struct Drag {
        AlignmentDragSurface surface;
        std::string edit_id;
        double start_distance;
        double min_steps;
        double max_steps;
        double candidate;
        double start_pointer = 0.0;
        double metres_per_pixel = 0.0;
        std::vector<TrackPoint> points;
    };
    struct Projection {
        double distance = 0.0;
        float distance_sq = std::numeric_limits<float>::infinity();
    };

    static CurveSpans build_spans(const std::vector<SourcedEvent>& events, double route_end,
                                  const std::map<std::string, AlignmentDraft>& drafts);
    const CurveSpans& curve_spans() const;
    static Preview calculate(const Request& request, const own_track_geometry::Trace& baseline,
                             const Matrix& baseline_samples);
    static Projection project(const TrackPoint& start, const TrackPoint& end, ImVec2 point);
    static Projection project(const std::vector<TrackPoint>& track, ImVec2 point);
    bool start_drag(AlignmentDragSurface surface, const std::string& id,
                    double distance_min, double distance_max);
    bool start_plan_drag(const std::string& id, const Matrix& geometry,
                         const PlanScreenTransform& transform, ImVec2 mouse,
                         double distance_min, double distance_max);
    void update_drag(double mileage_delta);
    template <typename MileageDelta>
    void process_drag(const MileageDelta& mileage_delta);
    void finish_drag();
    bool scroll_radius(const Matrix& geometry, const PlanScreenTransform& transform,
                       ImVec2 mouse, double distance_min, double distance_max,
                       float marker_scale, float wheel);
    void submit(std::chrono::milliseconds marker_delay = {});
    void queue_preview(std::chrono::milliseconds marker_delay = {});
    void run();

    std::shared_ptr<const Base> base_;
    std::uint64_t source_revision_ = 0;
    std::atomic<std::uint64_t> draft_revision_{0};
    std::map<std::string, AlignmentTarget> targets_;
    std::map<std::string, AlignmentDraft> drafts_;
    std::optional<Drag> drag_;
    std::string wheel_target_;
    ImVec2 wheel_point_;
    float wheel_remainder_ = 0.0f;
    std::optional<Result> published_;
    std::shared_ptr<const AlignmentReprojection> marker_projection_;
    std::uint64_t marker_revision_ = 0;
    AlignmentOverlay overlay_;
    std::vector<AlignmentRadiusLabel> visible_radius_labels_;
    std::uint64_t overlay_revision_ = 0;
    std::uint64_t overlay_plan_generation_ = 0;
    Metrics metrics_;
    bool measure_preview_ = false;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Request> pending_;
    std::optional<Result> completed_;
    std::size_t computed_ = 0;
    std::size_t discarded_results_ = 0;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace canvas2d
