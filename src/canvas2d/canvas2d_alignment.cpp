/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "canvas2d_alignment.h"
#include "canvas2d_marker_cache.h"
#include "canvas2d_primitives.h"
#include "imgui_internal.h"

#include <iterator>
#include <utility>

namespace canvas2d {
namespace {

constexpr double edit_step = 5.0;
constexpr auto wheel_marker_delay = std::chrono::milliseconds(32);

bool supports_endpoint(std::string_view method) {
    return method == "Curve.Begin" || method == "Curve.BeginCircular" ||
        method == "Curve.Change" || method == "Curve.End" ||
        method == "Curve.BeginTransition" || method == "Curve.Interpolate" ||
        method == "Legacy.Curve" ||
        method == "Gradient.Begin" || method == "Gradient.BeginConst" ||
        method == "Gradient.End" || method == "Gradient.BeginTransition" ||
        method == "Gradient.Interpolate";
}

bool supports_radius(std::string_view method) {
    return method == "Curve.Begin" || method == "Curve.BeginCircular" ||
        method == "Curve.Change" || method == "Curve.Interpolate" ||
        method == "Legacy.Curve";
}

double preceding_event(const std::vector<double>& distances, double distance) {
    const auto next = std::lower_bound(distances.begin(), distances.end(), distance);
    return next != distances.begin() ? *std::prev(next) : distance;
}

TrackPoint screen_point(const Matrix& track, std::size_t row,
                        const PlanScreenTransform& transform) {
    const ImVec2 screen = transform.model_to_screen(track.at(row, 1), track.at(row, 2));
    TrackPoint point;
    point.d = track.at(row, 0);
    point.x = screen.x;
    point.y = screen.y;
    return point;
}

float distance_squared(ImVec2 a, ImVec2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

std::optional<ImVec2> clipped_segment_midpoint(ImVec2 a, ImVec2 b, ImVec2 origin, ImVec2 size) {
    float start = 0.0f;
    float end = 1.0f;
    const auto clip_axis = [&](float from, float to, float min, float max) {
        if (from == to) return from >= min && from <= max;
        const float t0 = (min - from) / (to - from);
        const float t1 = (max - from) / (to - from);
        start = std::max(start, std::min(t0, t1));
        end = std::min(end, std::max(t0, t1));
        return start <= end;
    };
    if (!clip_axis(a.x, b.x, origin.x, origin.x + size.x) ||
        !clip_axis(a.y, b.y, origin.y, origin.y + size.y)) return std::nullopt;
    const float fraction = (start + end) * 0.5f;
    return ImVec2(a.x + fraction * (b.x - a.x), a.y + fraction * (b.y - a.y));
}

} // namespace

AlignmentEditor::~AlignmentEditor() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        pending_.reset();
    }
    condition_.notify_one();
    if (worker_.joinable()) worker_.join();
}

const AlignmentTarget* AlignmentEditor::target(const std::string& edit_id) const {
    const auto it = targets_.find(edit_id);
    return it != targets_.end() ? &it->second : nullptr;
}

void AlignmentEditor::set_source(const MapModel& model, std::uint64_t revision,
                                 const std::shared_ptr<const std::vector<RepeaterOverlayRow>>& repeaters) {
    const std::array<double, 3> distribution{model.cp_arb[0], model.cp_arb[1], model.cp_arb[2]};
    if (base_ && source_revision_ == revision &&
        base_->distribution == distribution) return;
    cancel_drag();
    targets_.clear();

    auto base = std::make_shared<Base>();
    base->distribution = distribution;
    base->repeaters = repeaters;
    base->other_tracks.reserve(model.other_tracks.size());
    for (const OtherTrack& track : model.other_tracks) {
        const auto& points = track.points;
        auto& line = base->other_tracks.emplace_back();
        line.rows = points.rows;
        line.cols = 4;
        line.data.reserve(line.rows * line.cols);
        for (std::size_t row = 0; row < points.rows; ++row) {
            const auto* first = points.data.data() + row * points.cols;
            line.data.insert(line.data.end(), first, first + 4);
        }
    }
    base->distances.reserve(model.own.rows);
    for (std::size_t row = 0; row < model.own.rows; ++row) {
        base->distances.push_back(model.own.at(row, 0));
    }
    base->events.reserve(model.own_events.size());
    const TrackEvent* last_radius = nullptr;
    for (const TrackEvent& event : model.own_events) {
        const bool gradient = event.key == "gradient";
        const bool radius = event.key == "radius";
        const TrackEvent* previous_radius = radius ? std::exchange(last_radius, &event) : nullptr;
        const auto& rows = gradient ? model.gradient_rows : model.curve_rows;
        const TableRow* source = event.source_row_index < rows.size()
            ? &rows[event.source_row_index] : nullptr;
        const std::string id = source ? source->edit_id : std::string{};
        const std::string_view method = source ? table_cell(*source, "method") : std::string_view{};
        const bool explicit_radius = radius && event.value_number &&
            supports_radius(method) && !id.empty();
        const double value = event.key == "interpolate_func"
            ? (event.text == "sin" ? 0.0 : 1.0) : event.number;
        base->events.push_back({
            {event.distance, event.key, {value, event.continues}, event.flag},
            event.source_order, id, explicit_radius});
        if (id.empty() || (!gradient && !radius) || !supports_endpoint(method) ||
            (event.flag == "bt" && table_cell(*source, "_transitionStatus") != "paired")) {
            continue;
        }
        AlignmentTarget target{
            gradient ? "gradient" : "curve", event.source_row_index, event.distance,
            explicit_radius ? std::optional<double>(event.number) : std::nullopt,
            method == "Gradient.Begin" && event.value_number
                ? std::optional<double>(event.number) : std::nullopt,
            event.source_order, {}};

        // Use the preceding geometry event, including for uncanted curves.
        const bool circular_endpoint = method == "Curve.Begin" || method == "Curve.BeginCircular" ||
            method == "Curve.Change" || method == "Curve.End";
        if (circular_endpoint && event.value_number && previous_radius &&
            previous_radius->flag == "bt" && previous_radius->source_row_index < model.curve_rows.size()) {
            const auto& transition_id = model.curve_rows[previous_radius->source_row_index].edit_id;
            if (!transition_id.empty()) {
                target.transition_edit_id = transition_id;
                targets_.try_emplace(transition_id, AlignmentTarget{
                    "curve", previous_radius->source_row_index, previous_radius->distance,
                    std::nullopt, std::nullopt, previous_radius->source_order, {}});
            }
        }
        targets_.emplace(id, std::move(target));
    }
    if (!base->distances.empty()) base->spans = build_spans(base->events, base->distances.back(), {});
    source_revision_ = revision;
    base_ = std::move(base);
    published_.reset();
    marker_projection_.reset();
    marker_revision_ = 0;
    wheel_target_.clear();
    submit();
}

AlignmentEditor::CurveSpans AlignmentEditor::build_spans(
    const std::vector<SourcedEvent>& events, double route_end,
    const std::map<std::string, AlignmentDraft>& drafts) {
    std::vector<const SourcedEvent*> radius_events;
    for (const SourcedEvent& event : events) {
        if (event.value.key == "radius") radius_events.push_back(&event);
    }
    CurveSpans spans;
    double inherited_radius = 0.0;
    for (std::size_t index = 0; index < radius_events.size(); ++index) {
        const SourcedEvent& from = *radius_events[index];
        const SourcedEvent* to = index + 1 < radius_events.size() ? radius_events[index + 1] : nullptr;
        if (!from.value.value.is_continue()) inherited_radius = from.value.value.number;
        const bool transition = to && !to->value.value.is_continue() &&
            (to->value.flag == "i" || from.value.flag == "bt");
        const SourcedEvent& owner = transition ? *to : from;
        const double radius = transition ? owner.value.value.number : inherited_radius;
        const auto draft = drafts.find(owner.edit_id);
        const bool retain_zero = draft != drafts.end() && draft->second.radius.has_value();
        const double end = to ? to->value.distance : route_end;
        if (from.value.distance >= end ||
            (radius == 0.0 && (!transition || inherited_radius == 0.0) && !retain_zero)) {
            continue;
        }
        spans.items.push_back({from.value.distance, end, transition,
                               owner.editable_radius ? owner.edit_id : std::string{},
                               !transition || owner.editable_radius ? format_double(radius, 6) : std::string{}});
        if (!spans.items.back().label.empty()) spans.label_order.push_back(spans.items.size() - 1);
    }
    std::stable_sort(spans.label_order.begin(), spans.label_order.end(), [&](std::size_t a, std::size_t b) {
        const auto& lhs = spans.items[a];
        const auto& rhs = spans.items[b];
        return std::tie(lhs.edit_id, lhs.transition) < std::tie(rhs.edit_id, rhs.transition);
    });
    return spans;
}

AlignmentEditor::Preview AlignmentEditor::calculate(
    const Request& request, const own_track_geometry::Trace& baseline,
    const Matrix& baseline_samples) {
    const Base& base = *request.base;
    std::vector<SourcedEvent> edited = base.events;
    std::vector<double> extra_distances;
    std::vector<double> radius_distances;
    std::vector<double> gradient_distances;
    for (const SourcedEvent& event : base.events) {
        if (event.value.key == "radius") radius_distances.push_back(event.value.distance);
        if (event.value.key == "gradient") gradient_distances.push_back(event.value.distance);
    }
    double affected = std::numeric_limits<double>::infinity();
    for (SourcedEvent& event : edited) {
        const auto it = request.drafts.find(event.edit_id);
        if (it == request.drafts.end()) continue;
        const AlignmentDraft& draft = it->second;
        const double original_distance = event.value.distance;
        if (draft.distance) {
            event.value.distance = *draft.distance;
            extra_distances.push_back(*draft.distance);
        }
        if (event.editable_radius && draft.radius) event.value.value.number = *draft.radius;
        if (draft.gradient) event.value.value.number = draft.gradient->value();
        const double boundary = std::min(original_distance, event.value.distance);
        const double projected_boundary = event.value.key == "gradient"
            ? preceding_event(gradient_distances, boundary) : boundary;
        affected = std::min(affected, preceding_event(radius_distances, projected_boundary));
    }
    std::sort(extra_distances.begin(), extra_distances.end());
    extra_distances.erase(std::unique(extra_distances.begin(), extra_distances.end()), extra_distances.end());
    std::vector<double> distances;
    distances.reserve(base.distances.size() + extra_distances.size());
    std::set_union(base.distances.begin(), base.distances.end(),
                   extra_distances.begin(), extra_distances.end(), std::back_inserter(distances));
    std::stable_sort(edited.begin(), edited.end(),
        [](const SourcedEvent& a, const SourcedEvent& b) {
            if (a.value.distance != b.value.distance) return a.value.distance < b.value.distance;
            return a.source_order < b.source_order;
        });
    std::vector<own_track_geometry::Event> events;
    events.reserve(edited.size());
    for (const SourcedEvent& event : edited) events.push_back(event.value);

    const std::size_t safe_rows = std::lower_bound(
        base.distances.begin(), base.distances.end(), affected) - base.distances.begin();
    const auto after_checkpoint = std::upper_bound(
        baseline.checkpoints.begin(), baseline.checkpoints.end(), safe_rows,
        [](std::size_t rows, const own_track_geometry::Checkpoint& state) { return rows < state.rows; });
    Preview result;
    auto geometry = std::make_shared<Matrix>();
    if (after_checkpoint == baseline.checkpoints.begin()) {
        geometry->data = own_track_geometry::sample(events, distances);
    } else {
        const auto& checkpoint = *std::prev(after_checkpoint);
        const own_track_geometry::Prefix prefix{checkpoint, baseline.event_projections};
        geometry->data = own_track_geometry::sample(events, distances, nullptr, &prefix);
        geometry->data.insert(geometry->data.begin(), baseline_samples.data.begin(),
            baseline_samples.data.begin() + checkpoint.rows * own_track_geometry::columns);
        result.reused_rows = checkpoint.rows;
    }
    geometry->rows = distances.size();
    geometry->cols = own_track_geometry::columns;
    result.geometry = std::move(geometry);
    result.spans = build_spans(edited, distances.back(), request.drafts);
    result.drafts = request.drafts;
    return result;
}

void AlignmentEditor::run() {
    std::shared_ptr<const Base> cached;
    own_track_geometry::Trace trace;
    std::shared_ptr<const Matrix> base_samples;
    std::uint64_t cached_revision = 0;
    std::shared_ptr<const Preview> cached_preview;
    const auto publish = [&](Result result, bool geometry_computed) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            computed_ += geometry_computed ? 1 : 0;
            if (result.revision != draft_revision_) {
                ++discarded_results_;
                return;
            }
            if (completed_ && completed_->revision != result.revision) ++discarded_results_;
            completed_ = std::move(result);
        }
        wake_main_window();
    };
    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_) return;
            request = std::move(*pending_);
            pending_.reset();
        }

        Result result;
        result.base = request.base;
        result.revision = request.revision;
        result.started = request.started;
        try {
            if (request.base != cached) {
                std::vector<own_track_geometry::Event> events;
                events.reserve(request.base->events.size());
                for (const SourcedEvent& event : request.base->events) events.push_back(event.value);
                trace = {};
                base_samples = std::make_shared<Matrix>(Matrix{
                    own_track_geometry::sample(events, request.base->distances, &trace),
                    request.base->distances.size(), own_track_geometry::columns});
                cached = request.base;
            }
            const bool geometry_changed = request.revision != cached_revision;
            if (geometry_changed) {
                cached_preview = std::make_shared<Preview>(calculate(request, trace, *base_samples));
                cached_revision = request.revision;
            }
            result.preview = cached_preview;
            if (geometry_changed) publish(result, true);
            if (!request.refresh_markers_at) continue;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait_until(lock, *request.refresh_markers_at, [&] {
                    return stopping_ || pending_.has_value() || request.revision != draft_revision_;
                });
                if (stopping_) return;
                if (pending_ || request.revision != draft_revision_) continue;
            }
            result.marker_projection = std::make_shared<AlignmentReprojection>(
                reproject_plan_geometry(base_samples, cached_preview->geometry,
                                        cached->other_tracks, *cached->repeaters));
        } catch (const std::exception& error) {
            result.error = error.what();
        }
        publish(std::move(result), false);
    }
}

void AlignmentEditor::submit(std::chrono::milliseconds marker_delay) {
    ++draft_revision_;
    queue_preview(marker_delay);
}

void AlignmentEditor::queue_preview(std::chrono::milliseconds marker_delay) {
    const bool needs_preview = dirty() && base_ && !base_->distances.empty();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_) ++metrics_.superseded;
        if (needs_preview) {
            const auto now = std::chrono::steady_clock::now();
            const auto refresh_at = dragging() ? std::nullopt : std::optional(now + marker_delay);
            pending_ = Request{base_, drafts_, draft_revision_, now, refresh_at};
            ++metrics_.submitted;
        } else {
            pending_.reset();
        }
    }
    if (needs_preview && !worker_.joinable()) worker_ = std::thread(&AlignmentEditor::run, this);
    if (!needs_preview) {
        published_.reset();
        marker_projection_.reset();
        marker_revision_ = 0;
    }
    condition_.notify_one();
    wake_main_window();
}

std::string AlignmentEditor::poll() {
    std::optional<Result> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.computed = computed_;
        metrics_.superseded += discarded_results_;
        discarded_results_ = 0;
        result.swap(completed_);
    }
    if (!result) return {};
    if (result->base != base_ || result->revision != draft_revision_) {
        ++metrics_.superseded;
        return {};
    }
    if (!result->error.empty()) {
        return std::move(result->error);
    }
    if (!published_ || published_->revision != result->revision) {
        metrics_.reused_rows += result->preview->reused_rows;
        if (measure_preview_) {
            const auto elapsed = std::chrono::steady_clock::now() - result->started;
            metrics_.latency_ms.push_back(std::chrono::duration<double, std::milli>(elapsed).count());
        }
    }
    if (result->marker_projection) {
        marker_projection_ = std::move(result->marker_projection);
        marker_revision_ = result->revision;
    }
    published_ = std::move(result);
    return {};
}

bool AlignmentEditor::current_preview_ready() const {
    return published_ && published_->revision == draft_revision_ &&
        (dragging() || marker_revision_ == draft_revision_);
}

void AlignmentEditor::clear_drafts() {
    drag_.reset();
    wheel_target_.clear();
    wheel_remainder_ = 0.0f;
    if (!dirty()) return;
    drafts_.clear();
    submit();
}

void AlignmentEditor::cancel_drag() {
    if (!drag_) return;
    if (set_drag_displacement(0.0)) {
        drag_.reset();
        submit();
    } else {
        finish_drag();
    }
}

void AlignmentEditor::set_distance(const std::string& id, double distance) {
    if (update_distance_draft(id, distance)) submit();
}

bool AlignmentEditor::update_distance_draft(const std::string& id, double distance) {
    const double original = targets_.at(id).distance;
    const auto it = drafts_.find(id);
    const std::optional<double> before = it != drafts_.end() ? it->second.distance : std::nullopt;
    const std::optional<double> after = distance != original
        ? std::optional<double>(distance) : std::nullopt;
    if (before == after) return false;
    AlignmentDraft& draft = drafts_[id];
    draft.distance = after;
    if (draft.empty()) drafts_.erase(id);
    return true;
}

void AlignmentEditor::step_radius(const std::string& id, int ticks) {
    const double original = targets_.at(id).radius.value();
    const auto it = drafts_.find(id);
    const double before = it != drafts_.end() ? it->second.radius.value_or(original) : original;
    double radius = before;
    const double step = ticks < 0 ? -edit_step : edit_step;
    for (int tick = 0; tick < std::abs(ticks); ++tick) {
        const double next = radius + step;
        radius = (radius > 0.0 && next < 0.0) || (radius < 0.0 && next > 0.0) ? 0.0 : next;
    }
    if (before == radius) return;
    AlignmentDraft& draft = drafts_[id];
    draft.radius = radius != original ? std::optional<double>(radius) : std::nullopt;
    if (draft.empty()) drafts_.erase(id);
    submit(wheel_marker_delay);
}

void AlignmentEditor::step_gradient(const std::string& id, int ticks) {
    AlignmentDraft& draft = drafts_[id];
    if (!draft.gradient) draft.gradient = GradientWheelDraft{targets_.at(id).gradient.value(), 0};
    draft.gradient->ticks += ticks;
    if (draft.gradient->ticks == 0) draft.gradient.reset();
    if (draft.empty()) drafts_.erase(id);
    submit(wheel_marker_delay);
}

const Matrix& AlignmentEditor::geometry(const Matrix& applied) const {
    return published_ ? *published_->preview->geometry : applied;
}

std::uint64_t AlignmentEditor::preview_revision() const {
    return published_ ? published_->revision : 0;
}

const AlignmentDraft* AlignmentEditor::preview_draft(const std::string& id) const {
    if (!published_) return nullptr;
    const auto& drafts = published_->preview->drafts;
    const auto it = drafts.find(id);
    return it == drafts.end() ? nullptr : &it->second;
}

const AlignmentEditor::CurveSpans& AlignmentEditor::curve_spans() const {
    return published_ ? published_->preview->spans : base_->spans;
}

const std::vector<AlignmentSpan>& AlignmentEditor::spans() const {
    return curve_spans().items;
}

const std::vector<AlignmentRadiusLabel>& AlignmentEditor::radius_labels(
    const Matrix& track, const PlanScreenTransform& transform,
    double distance_min, double distance_max, ImVec2 origin, ImVec2 size) {
    const auto& sections = curve_spans();
    auto& cache = radius_label_cache_;
    const auto preview_revision = published_ ? published_->revision : 0;
    if (cache.track != &track || cache.source_revision != source_revision_ ||
        cache.preview_revision != preview_revision || cache.distance_min != distance_min ||
        cache.distance_max != distance_max) {
        cache.candidates.clear();
        for (const std::size_t index : sections.label_order) {
            const auto& span = sections.items[index];
            const double start = std::max({span.start, distance_min, track.at(0, 0)});
            const double end = std::min({span.end, distance_max, track.at(track.rows - 1, 0)});
            if (start >= end) continue;
            cache.candidates.push_back({index,
                *sample_matrix_track_point(track, start, true),
                *sample_matrix_track_point(track, (start + end) * 0.5, true),
                *sample_matrix_track_point(track, end, true),
                matrix_upper_bound_distance(track, start), matrix_lower_bound_distance(track, end)});
        }
        cache.track = &track;
        cache.source_revision = source_revision_;
        cache.preview_revision = preview_revision;
        cache.distance_min = distance_min;
        cache.distance_max = distance_max;
    }

    const auto screen = [&](const TrackPoint& point) { return transform.model_to_screen(point.x, point.y); };
    const auto anchor = [&](const RadiusLabelCandidate& candidate) -> std::optional<ImVec2> {
        const ImVec2 middle = screen(candidate.middle);
        if (point_near_canvas(middle, origin, size, 0.0f)) return middle;
        ImVec2 a = screen(candidate.start);
        for (std::size_t row = candidate.first_row; row < candidate.last_row; ++row) {
            const ImVec2 b = transform.model_to_screen(track.at(row, 1), track.at(row, 2));
            if (auto visible = clipped_segment_midpoint(a, b, origin, size)) return visible;
            a = b;
        }
        return clipped_segment_midpoint(a, screen(candidate.end), origin, size);
    };
    visible_radius_labels_.clear();
    std::string_view shown_owner;
    for (const auto& candidate : cache.candidates) {
        const auto& span = sections.items[candidate.span];
        if (!span.edit_id.empty() && span.edit_id == shown_owner) continue;
        if (auto visible = anchor(candidate)) {
            visible_radius_labels_.push_back({&span, *visible});
            shown_owner = span.edit_id;
        }
    }
    return visible_radius_labels_;
}

const AlignmentOverlay* AlignmentEditor::overlay(
    const PlanData& plan, std::uint64_t plan_generation, const PlanScreenTransform& transform) {
    if (!published_) return nullptr;
    if (overlay_revision_ == published_->revision && overlay_plan_generation_ == plan_generation) {
        return &overlay_;
    }
    overlay_.curve_markers = plan.curve_edit_markers;
    overlay_.gradient_markers = plan.gradient_edit_markers;
    overlay_.interpolate_markers = plan.curve_interpolate_markers;

    auto locate = [&](auto& marker, const AlignmentDraft* draft) {
        if (draft && draft->distance) marker.d = *draft->distance;
        const auto point = sample_matrix_track_point(*published_->preview->geometry, marker.d, true);
        const auto [x, y] = transform.model_to_plan(point->x, point->y);
        marker.x = x;
        marker.y = y;
        return point.value();
    };
    for (auto& marker : overlay_.gradient_markers) {
        const auto* draft = preview_draft(marker.edit_id);
        locate(marker, draft);
        if (draft && draft->gradient) marker.label = format_double(draft->gradient->value(), 6);
    }
    for (auto& marker : overlay_.curve_markers) {
        const auto* draft = preview_draft(marker.edit_id);
        locate(marker, draft);
        if (draft && draft->radius) {
            const auto cant = marker.label.find(',');
            marker.label = format_double(*draft->radius, 6) +
                (cant != std::string::npos ? marker.label.substr(cant) : std::string{});
        }
    }
    for (auto& marker : overlay_.interpolate_markers) {
        const auto* draft = preview_draft(marker.edit_id);
        const auto point = locate(marker, draft);
        marker.theta = point.theta - plan.origin_angle;
        marker.radius = draft && draft->radius ? *draft->radius : point.radius;
        marker.label = format_double(marker.radius, 0);
    }

    overlay_revision_ = published_->revision;
    overlay_plan_generation_ = plan_generation;
    return &overlay_;
}

AlignmentEditor::Projection AlignmentEditor::project(
    const TrackPoint& start, const TrackPoint& end, ImVec2 point) {
    const double x = end.x - start.x;
    const double y = end.y - start.y;
    const double length_sq = x * x + y * y;
    const double fraction = length_sq > 0.0
        ? std::clamp(((point.x - start.x) * x + (point.y - start.y) * y) / length_sq, 0.0, 1.0)
        : 0.0;
    const ImVec2 nearest(static_cast<float>(start.x + fraction * x),
                         static_cast<float>(start.y + fraction * y));
    return {start.d + fraction * (end.d - start.d), distance_squared(nearest, point)};
}

AlignmentEditor::Projection AlignmentEditor::project(
    const std::vector<TrackPoint>& track, ImVec2 point) {
    Projection best;
    for (std::size_t row = 1; row < track.size(); ++row) {
        const Projection hit = project(track[row - 1], track[row], point);
        if (hit.distance_sq < best.distance_sq) best = hit;
    }
    return best;
}

bool AlignmentEditor::start_drag(AlignmentDragSurface surface, const std::string& id,
                                 double distance_min, double distance_max) {
    const AlignmentTarget& source = targets_.at(id);
    const auto current_distance = [&](const std::string& edit_id, double applied) {
        const auto draft = drafts_.find(edit_id);
        return draft != drafts_.end() ? draft->second.distance.value_or(applied) : applied;
    };
    const double origin = current_distance(id, source.distance);
    const std::string transition_id = surface == AlignmentDragSurface::Plan
        ? source.transition_edit_id : std::string{};
    const double transition_origin = transition_id.empty()
        ? origin : current_distance(transition_id, targets_.at(transition_id).distance);
    const std::string_view key = source.row_kind == "gradient" ? "gradient" : "radius";

    double lower = std::max({0.0, distance_min, base_->distances.front()});
    double upper = std::min(distance_max, base_->distances.back());
    for (const SourcedEvent& event : base_->events) {
        if (event.value.key != key || event.edit_id == id ||
            (!transition_id.empty() && event.edit_id == transition_id)) continue;
        const double distance = current_distance(event.edit_id, event.value.distance);
        if (std::tie(event.value.distance, event.source_order) <
            std::tie(source.distance, source.source_order)) {
            lower = std::max(lower, distance);
        } else {
            upper = std::min(upper, distance);
        }
    }
    const double min_steps = std::ceil((lower - std::min(origin, transition_origin)) / edit_step);
    const double max_steps = std::floor((upper - std::max(origin, transition_origin)) / edit_step);
    if (min_steps > max_steps || (min_steps == 0.0 && max_steps == 0.0)) return false;
    drag_ = Drag{surface, id, transition_id, origin, transition_origin,
                 min_steps, max_steps, origin, 0.0, 0.0, {}};
    return true;
}

bool AlignmentEditor::start_plan_drag(const std::string& id, const Matrix& geometry,
                                      const PlanScreenTransform& transform, ImVec2 mouse,
                                      double distance_min, double distance_max) {
    if (!start_drag(AlignmentDragSurface::Plan, id, distance_min, distance_max)) return false;
    const double lower = drag_->start_distance + drag_->min_steps * edit_step;
    const double upper = drag_->start_distance + drag_->max_steps * edit_step;
    const std::size_t lo = matrix_lower_bound_distance(geometry, lower);
    const std::size_t hi = matrix_upper_bound_distance(geometry, upper);
    const std::size_t first = lo == 0 ? 0 : lo - 1;
    const std::size_t last = std::min(hi + 1, geometry.rows);
    auto& track = drag_->points;
    track.reserve(last - first);
    for (std::size_t row = first; row < last; ++row) {
        track.push_back(screen_point(geometry, row, transform));
    }
    drag_->start_pointer = project(track, mouse).distance;
    return true;
}

bool AlignmentEditor::start_profile_drag(const std::string& id, double mouse_x,
                                         double metres_per_pixel, double distance_min,
                                         double distance_max) {
    if (!start_drag(AlignmentDragSurface::Profile, id, distance_min, distance_max)) return false;
    drag_->start_pointer = mouse_x;
    drag_->metres_per_pixel = metres_per_pixel;
    return true;
}

bool AlignmentEditor::set_drag_displacement(double delta) {
    drag_->candidate = drag_->start_distance + delta;
    bool changed = update_distance_draft(drag_->edit_id, drag_->candidate);
    if (!drag_->transition_edit_id.empty()) {
        changed |= update_distance_draft(drag_->transition_edit_id, drag_->transition_start_distance + delta);
    }
    return changed;
}

void AlignmentEditor::update_drag(double mileage_delta) {
    const double step = std::clamp(std::round(mileage_delta / edit_step),
                                   drag_->min_steps, drag_->max_steps);
    if (set_drag_displacement(edit_step * step)) submit();
}

void AlignmentEditor::finish_drag() {
    drag_.reset();
    if (dirty() && marker_revision_ != draft_revision_) queue_preview();
}

template <typename MileageDelta>
void AlignmentEditor::process_drag(const MileageDelta& mileage_delta) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancel_drag();
        return;
    }
    const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if ((held || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) && ImGui::IsMousePosValid() &&
        ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {
        update_drag(mileage_delta());
    }
    if (held) set_move_cursor();
    else finish_drag();
}

void AlignmentEditor::process_profile_drag(double mouse_x) {
    process_drag([&] { return (mouse_x - drag_->start_pointer) * drag_->metres_per_pixel; });
}

std::string_view AlignmentEditor::radius_wheel_target(
    const Matrix& geometry, const PlanScreenTransform& transform,
    ImVec2 mouse, double distance_min, double distance_max, float marker_scale) const {
    const float hit_radius = 5.0f * marker_scale;
    std::string_view owner;
    float nearest = hit_radius * hit_radius;
    const auto last = drafts_.find(wheel_target_);
    if (last != drafts_.end() && last->second.radius == 0.0 &&
        distance_squared(wheel_point_, mouse) <= nearest) {
        owner = wheel_target_;
    } else {
        for (const AlignmentSpan& span : spans()) {
            if (span.end < distance_min || span.start > distance_max) continue;
            const std::size_t first = matrix_lower_bound_distance(geometry, std::max(span.start, distance_min));
            const std::size_t last_row = matrix_upper_bound_distance(geometry, std::min(span.end, distance_max));
            const float width = (span.transition ? 4.0f : 5.0f) * marker_scale;
            for (std::size_t row = first + 1; row < last_row; ++row) {
                const auto hit = project(screen_point(geometry, row - 1, transform),
                                         screen_point(geometry, row, transform), mouse);
                if (hit.distance_sq > nearest || hit.distance_sq > width * width) continue;
                nearest = hit.distance_sq;
                owner = span.edit_id;
            }
        }
    }
    return owner;
}

bool AlignmentEditor::process_mouse(
    const PlanData& plan, const AlignmentOverlay* adjusted, const Matrix& track,
    const PlanScreenTransform& transform, ImVec2 mouse, double distance_min, double distance_max,
    float marker_scale, bool can_start, bool show_curves, PlanMarkerSelection& selection) {
    if (drag_) {
        if (dragging(AlignmentDragSurface::Plan)) {
            process_drag([&] { return project(drag_->points, mouse).distance - drag_->start_pointer; });
        }
        return true;
    }
    if (!can_start) {
        wheel_target_.clear();
        wheel_remainder_ = 0.0f;
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float hit_radius = std::max(12.0f, 12.0f * marker_scale);
    const auto& gradients = adjusted ? adjusted->gradient_markers : plan.gradient_edit_markers;
    if (!io.KeyShift && io.MouseWheel != 0.0f) {
        const OwnTrackEditMarker* closest = nullptr;
        float nearest = hit_radius * hit_radius;
        for (const auto& marker : gradients) {
            const float distance = distance_squared(transform.plan_to_screen(marker.x, marker.y), mouse);
            if (distance > nearest) continue;
            nearest = distance;
            closest = &marker;
        }
        const auto* gradient = closest ? target(closest->edit_id) : nullptr;
        const bool edit_gradient = gradient && gradient->gradient;
        std::string_view owner;
        if (edit_gradient) {
            owner = closest->edit_id;
        } else if (show_curves) {
            owner = radius_wheel_target(track, transform, mouse, distance_min, distance_max, marker_scale);
        }
        if (owner != wheel_target_) {
            wheel_target_ = owner;
            wheel_remainder_ = 0.0f;
        }
        if (!wheel_target_.empty()) {
            wheel_point_ = mouse;
            wheel_remainder_ += io.MouseWheel;
            const int ticks = static_cast<int>(wheel_remainder_);
            wheel_remainder_ -= static_cast<float>(ticks);
            if (ticks != 0) {
                if (edit_gradient) step_gradient(wheel_target_, ticks);
                else step_radius(wheel_target_, ticks);
            }
            return true;
        }
    }

    std::string_view id;
    PlanMarkerSelection hovered;
    float nearest = hit_radius * hit_radius;
    bool selected = false;
    auto test = [&](const auto& markers, PlanMarkerKind kind) {
        for (const auto& marker : markers) {
            const float distance = distance_squared(transform.plan_to_screen(marker.x, marker.y), mouse);
            const bool preferred = selection.matches(kind, marker.row_index);
            if (distance > hit_radius * hit_radius || (selected && !preferred) ||
                (preferred == selected && distance > nearest) || !target(marker.edit_id)) continue;
            hovered = {kind, marker.row_index};
            id = marker.edit_id;
            nearest = distance;
            selected = preferred;
        }
    };
    test(adjusted ? adjusted->curve_markers : plan.curve_edit_markers, PlanMarkerKind::Curve);
    test(gradients, PlanMarkerKind::Gradient);
    test(adjusted ? adjusted->interpolate_markers : plan.curve_interpolate_markers, PlanMarkerKind::Curve);
    if (id.empty()) return false;
    if (!io.KeyCtrl) set_move_cursor();
    if (io.KeyCtrl || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
    if (!start_plan_drag(std::string(id), track, transform, mouse, distance_min, distance_max)) return false;
    selection = hovered;
    return true;
}

void AlignmentEditor::draw_drag_label(AlignmentDragSurface surface, ImDrawList* draw,
                                      ImVec2 mouse, float marker_scale) const {
    if (!dragging(surface)) return;
    if (surface == AlignmentDragSurface::Plan && !current_preview_ready()) {
        const auto second = std::upper_bound(drag_->points.begin(), drag_->points.end(), drag_->candidate,
            [](double distance, const TrackPoint& point) { return distance < point.d; });
        const auto end = std::clamp(second, drag_->points.begin() + 1, drag_->points.end() - 1);
        const auto start = std::prev(end);
        const double fraction = (drag_->candidate - start->d) / (end->d - start->d);
        const ImVec2 marker(static_cast<float>(start->x + fraction * (end->x - start->x)),
                            static_cast<float>(start->y + fraction * (end->y - start->y)));
        draw->AddCircle(marker, 7.0f * marker_scale, IM_COL32(136, 255, 136, 255), 16, 1.5f);
    }
    const std::string text = format_double(drag_->candidate, 6) + " m";
    const ImVec2 origin(mouse.x + 15.0f * marker_scale, mouse.y + 4.0f);
    const ImVec2 extent = ImGui::CalcTextSize(text.c_str());
    draw->AddRectFilled(ImVec2(origin.x - 3.0f, origin.y - 2.0f),
                        ImVec2(origin.x + extent.x + 3.0f, origin.y + extent.y + 2.0f),
                        IM_COL32(12, 13, 15, 235), 3.0f);
    draw->AddText(origin, IM_COL32(245, 245, 245, 255), text.c_str());
}

} // namespace canvas2d
