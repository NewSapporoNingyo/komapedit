/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "canvas2d_alignment.h"
#include "canvas2d_marker_cache.h"
#include "canvas2d_primitives.h"

#include <iterator>

namespace canvas2d {
namespace {

constexpr double edit_step = 5.0;

bool supports_endpoint(std::string_view method) {
    return method == "Curve.Begin" || method == "Curve.BeginCircular" ||
        method == "Curve.Change" || method == "Curve.End" ||
        method == "Curve.BeginTransition" || method == "Curve.Interpolate" ||
        method == "Gradient.Begin" || method == "Gradient.BeginConst" ||
        method == "Gradient.End" || method == "Gradient.BeginTransition" ||
        method == "Gradient.Interpolate";
}

bool supports_radius(std::string_view method) {
    return method == "Curve.Begin" || method == "Curve.BeginCircular" ||
        method == "Curve.Change" || method == "Curve.Interpolate";
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

void AlignmentEditor::set_source(const MapModel& model, std::uint64_t revision) {
    const std::array<double, 3> distribution{model.cp_arb[0], model.cp_arb[1], model.cp_arb[2]};
    if (base_ && source_revision_ == revision &&
        base_->distribution == distribution) return;
    cancel_drag();
    targets_.clear();

    auto base = std::make_shared<Base>();
    base->distribution = distribution;
    base->distances.reserve(model.own.rows);
    for (std::size_t row = 0; row < model.own.rows; ++row) {
        base->distances.push_back(model.own.at(row, 0));
    }
    base->events.reserve(model.own_events.size());
    for (const TrackEvent& event : model.own_events) {
        const bool gradient = event.key == "gradient";
        const auto& rows = gradient ? model.gradient_rows : model.curve_rows;
        const TableRow* source = event.source_row_index < rows.size()
            ? &rows[event.source_row_index] : nullptr;
        const std::string id = source ? source->edit_id : std::string{};
        const std::string method = source ? table_cell(*source, "method") : std::string{};
        const bool explicit_radius = event.key == "radius" && event.value_number &&
            supports_radius(method) && !id.empty();
        const double value = event.key == "interpolate_func"
            ? (event.text == "sin" ? 0.0 : 1.0) : event.number;
        base->events.push_back({
            {event.distance, event.key, {value, event.continues}, event.flag},
            event.source_order, id, explicit_radius});
        if (!id.empty() && (gradient || event.key == "radius") &&
            supports_endpoint(method) &&
            (event.flag != "bt" || table_cell(*source, "_transitionStatus") == "paired")) {
            targets_.emplace(id, AlignmentTarget{
                gradient ? "gradient" : "curve", event.source_row_index, event.distance,
                explicit_radius ? std::optional<double>(event.number) : std::nullopt,
                event.source_order});
        }
    }
    if (!base->distances.empty()) base->spans = build_spans(base->events, base->distances.back(), {});
    source_revision_ = revision;
    base_ = std::move(base);
    published_.reset();
    wheel_target_.clear();
    submit();
}

std::vector<AlignmentSpan> AlignmentEditor::build_spans(
    const std::vector<SourcedEvent>& events, double route_end,
    const std::map<std::string, AlignmentDraft>& drafts) {
    std::vector<const SourcedEvent*> radius_events;
    for (const SourcedEvent& event : events) {
        if (event.value.key == "radius") radius_events.push_back(&event);
    }
    std::vector<AlignmentSpan> spans;
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
        spans.push_back({from.value.distance, end, radius, transition,
                         owner.editable_radius ? owner.edit_id : std::string{},
                         !transition || owner.editable_radius ? format_double(radius, 6) : std::string{}});
    }
    return spans;
}

AlignmentEditor::Result AlignmentEditor::calculate(
    Request request, const own_track_geometry::Trace& baseline,
    const std::vector<double>& baseline_samples) {
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
    Result result;
    if (after_checkpoint == baseline.checkpoints.begin()) {
        result.geometry.data = own_track_geometry::sample(events, distances);
    } else {
        const auto& checkpoint = *std::prev(after_checkpoint);
        const own_track_geometry::Prefix prefix{checkpoint, baseline.event_projections};
        result.geometry.data = own_track_geometry::sample(events, distances, nullptr, &prefix);
        result.geometry.data.insert(result.geometry.data.begin(), baseline_samples.begin(),
            baseline_samples.begin() + checkpoint.rows * own_track_geometry::columns);
        result.reused_rows = checkpoint.rows;
    }
    result.geometry.rows = distances.size();
    result.geometry.cols = own_track_geometry::columns;
    result.spans = build_spans(edited, distances.back(), request.drafts);
    result.base = std::move(request.base);
    result.drafts = std::move(request.drafts);
    result.revision = request.revision;
    result.started = request.started;
    return result;
}

void AlignmentEditor::run() {
    std::shared_ptr<const Base> cached;
    own_track_geometry::Trace trace;
    std::vector<double> base_samples;
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
        try {
            if (request.base != cached) {
                std::vector<own_track_geometry::Event> events;
                events.reserve(request.base->events.size());
                for (const SourcedEvent& event : request.base->events) events.push_back(event.value);
                trace = {};
                base_samples = own_track_geometry::sample(events, request.base->distances, &trace);
                cached = request.base;
            }
            result = calculate(request, trace, base_samples);
        } catch (const std::exception& error) {
            result.base = request.base;
            result.revision = request.revision;
            result.error = error.what();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            ++computed_;
            if (completed_) ++discarded_results_;
            completed_ = std::move(result);
        }
        wake_main_window();
    }
}

void AlignmentEditor::submit() {
    ++draft_revision_;
    const bool needs_preview = dirty() && base_ && !base_->distances.empty();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_) ++metrics_.superseded;
        if (needs_preview) {
            pending_ = Request{base_, drafts_, draft_revision_, std::chrono::steady_clock::now()};
            ++metrics_.submitted;
        } else {
            pending_.reset();
        }
    }
    if (needs_preview && !worker_.joinable()) worker_ = std::thread(&AlignmentEditor::run, this);
    if (!needs_preview) published_.reset();
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
    metrics_.reused_rows += result->reused_rows;
    if (measure_preview_) {
        const auto elapsed = std::chrono::steady_clock::now() - result->started;
        metrics_.latency_ms.push_back(std::chrono::duration<double, std::milli>(elapsed).count());
    }
    published_ = std::move(result);
    return {};
}

bool AlignmentEditor::current_preview_ready() const {
    return published_ && published_->revision == draft_revision_;
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
    const auto value = drag_->previous_distance.value_or(drag_->start_distance);
    const std::string id = std::move(drag_->edit_id);
    drag_.reset();
    set_distance(id, value);
}

void AlignmentEditor::set_distance(const std::string& id, double distance) {
    const double original = targets_.at(id).distance;
    const auto it = drafts_.find(id);
    const std::optional<double> before = it != drafts_.end() ? it->second.distance : std::nullopt;
    const std::optional<double> after = distance != original
        ? std::optional<double>(distance) : std::nullopt;
    if (before == after) return;
    AlignmentDraft& draft = drafts_[id];
    draft.distance = after;
    if (!draft.distance && !draft.radius) drafts_.erase(id);
    submit();
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
    if (!draft.distance && !draft.radius) drafts_.erase(id);
    submit();
}

const Matrix& AlignmentEditor::geometry(const Matrix& applied) const {
    return published_ ? published_->geometry : applied;
}

const std::vector<AlignmentSpan>& AlignmentEditor::spans() const {
    return published_ ? published_->spans : base_->spans;
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

    auto locate = [&](auto& marker) {
        const auto draft = published_->drafts.find(marker.edit_id);
        if (draft != published_->drafts.end() && draft->second.distance) marker.d = *draft->second.distance;
        const auto point = sample_matrix_track_point(published_->geometry, marker.d, true);
        const auto [x, y] = transform.model_to_plan(point->x, point->y);
        marker.x = x;
        marker.y = y;
        return point.value();
    };
    for (auto& marker : overlay_.gradient_markers) locate(marker);
    for (auto& marker : overlay_.curve_markers) {
        locate(marker);
        const auto draft = published_->drafts.find(marker.edit_id);
        if (draft != published_->drafts.end() && draft->second.radius) {
            const auto cant = marker.label.find(',');
            marker.label = format_double(*draft->second.radius, 6) +
                (cant != std::string::npos ? marker.label.substr(cant) : std::string{});
        }
    }
    for (auto& marker : overlay_.interpolate_markers) {
        const auto point = locate(marker);
        marker.theta = point.theta - plan.origin_angle;
        const auto draft = published_->drafts.find(marker.edit_id);
        marker.radius = draft != published_->drafts.end() && draft->second.radius
            ? *draft->second.radius : point.radius;
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

bool AlignmentEditor::start_drag(const std::string& id, const Matrix& geometry,
                                 const PlanScreenTransform& transform, ImVec2 mouse,
                                 double distance_min, double distance_max) {
    const AlignmentTarget& source = targets_.at(id);
    const auto current = drafts_.find(id);
    const auto previous = current != drafts_.end() ? current->second.distance : std::nullopt;
    const double origin = previous.value_or(source.distance);
    const std::string_view key = source.row_kind == "gradient" ? "gradient" : "radius";

    double lower = std::max({0.0, distance_min, geometry.at(0, 0)});
    double upper = std::min(distance_max, geometry.at(geometry.rows - 1, 0));
    for (const SourcedEvent& event : base_->events) {
        if (event.value.key != key || event.edit_id == id) continue;
        const auto draft = drafts_.find(event.edit_id);
        const double distance = draft != drafts_.end()
            ? draft->second.distance.value_or(event.value.distance) : event.value.distance;
        if (std::tie(event.value.distance, event.source_order) <
            std::tie(source.distance, source.source_order)) {
            lower = std::max(lower, distance);
        } else {
            upper = std::min(upper, distance);
        }
    }
    const double min_steps = std::ceil((lower - origin) / edit_step);
    const double max_steps = std::floor((upper - origin) / edit_step);
    if (min_steps > max_steps || (min_steps == 0.0 && max_steps == 0.0)) return false;

    const std::size_t lo = matrix_lower_bound_distance(geometry, lower);
    const std::size_t hi = matrix_upper_bound_distance(geometry, upper);
    const std::size_t first = lo == 0 ? 0 : lo - 1;
    const std::size_t last = std::min(hi + 1, geometry.rows);
    std::vector<TrackPoint> track;
    track.reserve(last - first);
    for (std::size_t row = first; row < last; ++row) {
        track.push_back(screen_point(geometry, row, transform));
    }
    if (track.size() < 2) return false;
    const double projection = project(track, mouse).distance;
    drag_ = Drag{id, previous, origin, projection, min_steps, max_steps, origin, std::move(track)};
    return true;
}

bool AlignmentEditor::scroll_radius(
    const Matrix& geometry, const PlanScreenTransform& transform,
    ImVec2 mouse, double distance_min, double distance_max, float marker_scale, float wheel) {
    const float hit_radius = 5.0f * marker_scale;
    std::string owner;
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
    if (owner != wheel_target_) wheel_remainder_ = 0.0f;
    wheel_target_ = owner;
    if (wheel_target_.empty()) return false;
    wheel_point_ = mouse;
    wheel_remainder_ += wheel;
    const int ticks = static_cast<int>(wheel_remainder_);
    wheel_remainder_ -= static_cast<float>(ticks);
    if (ticks != 0) step_radius(wheel_target_, ticks);
    return true;
}

bool AlignmentEditor::process_mouse(
    const PlanData& plan, const AlignmentOverlay* adjusted, const Matrix& track,
    const PlanScreenTransform& transform, ImVec2 mouse, double distance_min, double distance_max,
    float marker_scale, bool can_start, bool show_curves, PlanMarkerSelection& selection) {
    if (drag_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancel_drag();
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            drag_.reset();
        } else {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const double distance = project(drag_->points, mouse).distance;
                const double step = std::clamp(std::round((distance - drag_->start_projection) / edit_step),
                                               drag_->min_steps, drag_->max_steps);
                drag_->candidate = drag_->start_distance + edit_step * step;
                set_distance(drag_->edit_id, drag_->candidate);
            }
            set_move_cursor();
        }
        return true;
    }
    if (!can_start) {
        wheel_target_.clear();
        wheel_remainder_ = 0.0f;
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (show_curves && !io.KeyShift && io.MouseWheel != 0.0f &&
        scroll_radius(track, transform, mouse, distance_min, distance_max, marker_scale, io.MouseWheel)) {
        return true;
    }

    const float hit_radius = std::max(12.0f, 12.0f * marker_scale);
    std::string_view id;
    PlanMarkerSelection hovered;
    float nearest = hit_radius * hit_radius;
    bool selected = false;
    auto test = [&](const auto& markers, PlanMarkerKind kind) {
        for (const auto& marker : markers) {
            if (targets_.find(marker.edit_id) == targets_.end()) continue;
            const float distance = distance_squared(transform.plan_to_screen(marker.x, marker.y), mouse);
            const bool preferred = selection.matches(kind, marker.row_index);
            if (distance > hit_radius * hit_radius || (selected && !preferred) ||
                (preferred == selected && distance > nearest)) continue;
            hovered = {kind, marker.row_index};
            id = marker.edit_id;
            nearest = distance;
            selected = preferred;
        }
    };
    test(adjusted ? adjusted->curve_markers : plan.curve_edit_markers, PlanMarkerKind::Curve);
    test(adjusted ? adjusted->gradient_markers : plan.gradient_edit_markers, PlanMarkerKind::Gradient);
    test(adjusted ? adjusted->interpolate_markers : plan.curve_interpolate_markers, PlanMarkerKind::Curve);
    if (id.empty()) return false;
    if (!io.KeyCtrl) set_move_cursor();
    if (io.KeyCtrl || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
    if (!start_drag(std::string(id), track, transform, mouse, distance_min, distance_max)) return false;
    selection = hovered;
    return true;
}

void AlignmentEditor::draw_drag_label(ImDrawList* draw, ImVec2 mouse, float marker_scale) const {
    if (!drag_) return;
    if (!current_preview_ready()) {
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
