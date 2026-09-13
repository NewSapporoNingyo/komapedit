/*
 * Copyright (c) 2026 Sapporo_ningyo
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace kme::timing {

// Separate domains deliberately keep DLL timing out of the GUI's TLS state.
// Stages are inclusive, aggregated by name, and never used for control flow.
template<class Domain> class Timing {
public:
    using Clock = std::chrono::steady_clock;
    struct Sample { std::string name; double seconds = 0; std::uint64_t count = 0; };
    struct Trace {
        Clock::time_point start = Clock::now();
        std::vector<Sample> samples;
        double seconds() const { return std::chrono::duration<double>(Clock::now() - start).count(); }
        std::uint64_t count(const char* name) const {
            const auto found = std::find_if(samples.begin(), samples.end(),
                [name](const Sample& item) { return item.name == name; });
            return found == samples.end() ? 0 : found->count;
        }
        void add(const char* name, double seconds) noexcept {
            try {
                auto found = std::find_if(samples.begin(), samples.end(),
                    [name](const Sample& item) { return item.name == name; });
                if (found == samples.end()) samples.push_back({name, seconds, 1});
                else { found->seconds += seconds; ++found->count; }
            } catch (...) { /* Diagnostics must not change edit success. */ }
        }
        std::string format(const std::string& operation, double total,
                           const std::string& outcome) const {
            std::ostringstream out;
            out << std::fixed << std::setprecision(3)
                << "edit timing: " << operation << " outcome=" << outcome
                << " total_ms=" << total * 1000.0 << " stages=inclusive";
            for (const Sample& sample : samples) {
                out << " " << sample.name << "_ms=" << sample.seconds * 1000.0
                    << " " << sample.name << "_count=" << sample.count;
            }
            return out.str();
        }
    };
    class Activation {
    public:
        explicit Activation(Trace* trace) : previous_(active_) { active_ = trace; }
        ~Activation() { reset(); }
        void reset() { if (!reset_) { active_ = previous_; reset_ = true; } }
        Activation(const Activation&) = delete;
        Activation& operator=(const Activation&) = delete;
    private:
        Trace* previous_;
        bool reset_ = false;
    };
    class Stage {
    public:
        explicit Stage(const char* name) : trace_(active_), name_(name), start_(Clock::now()) {}
        ~Stage() { finish(); }
        void next(const char* name) { finish(); name_ = name; start_ = Clock::now(); }
        void finish() noexcept {
            if (trace_ && name_) trace_->add(name_, std::chrono::duration<double>(Clock::now() - start_).count());
            name_ = nullptr;
        }
        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;
    private:
        Trace* trace_;
        const char* name_;
        Clock::time_point start_;
    };
    class Operation {
    public:
        Operation(const char* name, std::function<void(const std::string&)> sink)
            : activation_(&trace_), name_(name), sink_(std::move(sink)) {}
        ~Operation() noexcept {
            try { sink_(trace_.format(name_, trace_.seconds(), outcome)); } catch (...) {}
        }
        std::string outcome = "failed";
    private:
        Trace trace_;
        Activation activation_;
        const char* name_;
        std::function<void(const std::string&)> sink_;
    };
private:
    inline static thread_local Trace* active_ = nullptr;
};
struct GuiDomain;
struct MapDomain;
using GuiTiming = Timing<GuiDomain>;
using MapTiming = Timing<MapDomain>;
}
