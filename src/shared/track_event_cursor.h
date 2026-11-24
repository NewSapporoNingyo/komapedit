/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <cmath>
#include <string_view>
#include <vector>

namespace own_track_geometry {

template <typename Event>
class TrackPointer {
public:
    TrackPointer(const std::vector<Event>& data, std::string_view target, int last = -1)
        : data_(data), target_(target), last_(last), next_(seek(last + 1)) {}

    int last() const { return last_; }
    int next() const { return next_; }

    bool on_nextpoint(double distance) const {
        return next_ >= 0 && event(next_).distance == distance;
    }

    bool over_nextpoint(double distance) const {
        return next_ >= 0 && event(next_).distance < distance;
    }

    void seeknext() {
        if (next_ >= 0) {
            last_ = next_;
            next_ = seek(next_ + 1);
        }
    }

    int seekoriginofcontinuous(int index) const {
        while (index >= 0) {
            const auto& e = event(index);
            if (e.key == target_ && !e.value.is_continue()) return index;
            --index;
        }
        return -1;
    }

    const Event& event(int index) const { return data_[index]; }

private:
    int seek(int ix) const {
        const int count = static_cast<int>(data_.size());
        while (ix < count) {
            if (event(ix).key == target_) return ix;
            ++ix;
        }
        return -1;
    }

    const std::vector<Event>& data_;
    std::string_view target_;
    int last_ = -1;
    int next_;
};

template <typename Event>
class CantProcessor {
public:
    CantProcessor(TrackPointer<Event> pointer, double last_cant)
        : pointer_(pointer), cant_value_(last_cant) {}

    int last_event() const { return pointer_.last(); }
    double current_value() const { return cant_value_; }

    double process(double dist, bool half_sine) {
        while (pointer_.over_nextpoint(dist)) {
            int origin = pointer_.seekoriginofcontinuous(pointer_.next());
            if (origin >= 0) {
                cant_value_ = as_number(pointer_.event(origin).value);
            }
            pointer_.seeknext();
        }

        if (pointer_.last() < 0 || pointer_.next() < 0) return cant_value_;
        const Event& next = pointer_.event(pointer_.next());
        if (next.value.is_continue()) return cant_value_;
        const Event& last = pointer_.event(pointer_.last());
        if (next.flag == "i" || last.flag == "bt") {
            double next_value = as_number(next.value);
            if (cant_value_ != next_value) {
                return transition(next.distance - last.distance, cant_value_, next_value,
                                  half_sine, dist - last.distance);
            }
        }
        return cant_value_;
    }

private:
    static double transition(double length, double first, double last,
                             bool half_sine, double distance) {
        constexpr double pi = 3.141592653589793238462643383279502884;
        if (length == 0.0) return last;
        if (half_sine) {
            return (last - first) / 2.0 *
                (std::sin(pi / length * distance - pi / 2.0) + 1.0) + first;
        }
        return (last - first) / length * distance + first;
    }

    TrackPointer<Event> pointer_;
    double cant_value_ = 0.0;
};

} // namespace own_track_geometry
