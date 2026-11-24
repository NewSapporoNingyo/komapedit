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

struct AlignmentOverlay {
    std::vector<OwnTrackEditMarker> curve_markers;
    std::vector<OwnTrackEditMarker> gradient_markers;
    std::vector<PlanCurveInterpolateMarker> interpolate_markers;
};

class AlignmentEditor {
public:
    AlignmentEditor() = default;
    ~AlignmentEditor();

    bool enabled = false;
    bool dragging() const { return drag_.has_value(); }
    bool dirty() const { return !drafts_.empty(); }
    bool current_preview_ready() const;
    const auto& drafts() const { return drafts_; }
    const AlignmentTarget* target(const std::string& edit_id) const;

    void set_source(const MapModel& model, std::uint64_t revision);
    std::string poll();
    void clear_drafts();
    void cancel_drag();
    void set_distance(const std::string& edit_id, double distance);
    void step_radius(const std::string& edit_id, int ticks);

    const Matrix& geometry(const Matrix& applied) const;
    const std::vector<AlignmentSpan>& spans() const;
    const AlignmentOverlay* overlay(const PlanData& plan, std::uint64_t plan_generation,
                                    const PlanScreenTransform& transform);
    bool process_mouse(const PlanData& plan, const AlignmentOverlay* overlay,
                       const Matrix& geometry, const PlanScreenTransform& transform,
                       ImVec2 mouse, double distance_min, double distance_max,
                       float marker_scale, bool can_start, bool show_curves,
                       PlanMarkerSelection& selection);
    void draw_drag_label(ImDrawList* draw, ImVec2 mouse, float marker_scale) const;

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
    struct SourcedEvent {
        own_track_geometry::Event value;
        int source_order;
        std::string edit_id;
        bool editable_radius;
    };
    struct Base {
        std::vector<double> distances;
        std::vector<SourcedEvent> events;
        std::vector<AlignmentSpan> spans;
        std::array<double, 3> distribution;
    };
    struct Request {
        std::shared_ptr<const Base> base;
        std::map<std::string, AlignmentDraft> drafts;
        std::uint64_t revision;
        std::chrono::steady_clock::time_point started;
    };
    struct Result {
        Matrix geometry;
        std::vector<AlignmentSpan> spans;
        std::map<std::string, AlignmentDraft> drafts;
        std::shared_ptr<const Base> base;
        std::uint64_t revision = 0;
        std::chrono::steady_clock::time_point started;
        std::size_t reused_rows = 0;
        std::string error;
    };
    struct Drag {
        std::string edit_id;
        std::optional<double> previous_distance;
        double start_distance;
        double start_projection;
        double min_steps;
        double max_steps;
        double candidate;
        std::vector<TrackPoint> points;
    };
    struct Projection {
        double distance = 0.0;
        float distance_sq = std::numeric_limits<float>::infinity();
    };

    static std::vector<AlignmentSpan> build_spans(const std::vector<SourcedEvent>& events,
                                                 double route_end,
                                                 const std::map<std::string, AlignmentDraft>& drafts);
    static Result calculate(Request request, const own_track_geometry::Trace& baseline,
                            const std::vector<double>& baseline_samples);
    static Projection project(const TrackPoint& start, const TrackPoint& end, ImVec2 point);
    static Projection project(const std::vector<TrackPoint>& track, ImVec2 point);
    bool start_drag(const std::string& id, const Matrix& geometry,
                    const PlanScreenTransform& transform, ImVec2 mouse,
                    double distance_min, double distance_max);
    bool scroll_radius(const Matrix& geometry, const PlanScreenTransform& transform,
                       ImVec2 mouse, double distance_min, double distance_max,
                       float marker_scale, float wheel);
    void submit();
    void run();

    std::shared_ptr<const Base> base_;
    std::uint64_t source_revision_ = 0;
    std::uint64_t draft_revision_ = 0;
    std::map<std::string, AlignmentTarget> targets_;
    std::map<std::string, AlignmentDraft> drafts_;
    std::optional<Drag> drag_;
    std::string wheel_target_;
    ImVec2 wheel_point_;
    float wheel_remainder_ = 0.0f;
    std::optional<Result> published_;
    AlignmentOverlay overlay_;
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
