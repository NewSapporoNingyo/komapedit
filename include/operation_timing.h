/*
 * Copyright (c) 2026 Sapporo_ningyo
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace kme::timing {

#define KME_STAGE_ENUM(key, text) key,
#define KME_STAGE_NAME(key, text) text,

#define KME_GUI_TIMING_STAGES(X) \
    X(TableInvalidate, "table.invalidate") \
    X(PlanMarkers, "plan.markers") \
    X(SceneMarkerGpu, "scene.marker_gpu") \
    X(SceneChunks, "scene.chunks") \
    X(SceneRebuild, "scene.rebuild") \
    X(SceneGpuModels, "scene.gpu_models") \
    X(SceneFirstFrame, "scene.first_frame") \
    X(InsertPrepareApply, "insert.prepare_apply") \
    X(InsertApply, "insert.apply") \
    X(SnapshotHydrate, "snapshot.hydrate") \
    X(InspectorReload, "inspector.reload") \
    X(RenamePrepare, "rename.prepare") \
    X(RenameApply, "rename.apply") \
    X(InspectorPrepareApply, "inspector.prepare_apply") \
    X(InspectorApply, "inspector.apply") \
    X(ListPrepareApply, "list.prepare_apply") \
    X(ListApply, "list.apply") \
    X(ListFinish, "list.finish") \
    X(SceneMarkerRecipes, "scene.marker_recipes") \
    X(RefreshLocal, "refresh.local") \
    X(SceneMapContent, "scene.map_content") \
    X(RefreshTablesMarkers, "refresh.tables_markers") \
    X(ScenePlacements, "scene.placements") \
    X(SceneDynamicContent, "scene.dynamic_content") \
    X(DeletePrepareApply, "delete.prepare_apply") \
    X(DeleteApply, "delete.apply") \
    X(BackendReset, "backend.reset") \
    X(BackendBatch, "backend.batch") \
    X(BackendApply, "backend.apply") \
    X(BackendReport, "backend.report") \
    X(PreviewBackup, "preview.backup") \
    X(PreviewBackend, "preview.backend") \
    X(PreviewRollback, "preview.rollback") \
    X(PreviewHydration, "preview.hydration") \
    X(SnapshotPartialRefresh, "snapshot.partial_refresh") \
    X(SnapshotGet, "snapshot.get") \
    X(SnapshotFullRefresh, "snapshot.full_refresh") \
    X(PreviewRows, "preview.rows") \
    X(PreviewRefresh, "preview.refresh") \
    X(PreviewText, "preview.text") \
    X(SavePrepare, "save.prepare") \
    X(SaveCommit, "save.commit") \
    X(SaveMetadata, "save.metadata") \
    X(SaveText, "save.text") \
    X(SaveInspector, "save.inspector") \
    X(SceneDynamic, "scene.dynamic") \
    X(SceneModelRequests, "scene.model_requests")
enum class GuiStage { KME_GUI_TIMING_STAGES(KME_STAGE_ENUM) Count };
inline constexpr std::array k_gui_stage_names{KME_GUI_TIMING_STAGES(KME_STAGE_NAME)};
#undef KME_GUI_TIMING_STAGES

#define KME_MAP_TIMING_STAGES(X) \
    X(TypedSnapshotBuild, "typed.snapshot_build") \
    X(ParseIncludeJoinMerge, "parse.include_join_merge") \
    X(ParseSetup, "parse.setup") \
    X(ParseSyntaxDiagnostics, "parse.syntax_diagnostics") \
    X(ParseRelocate, "parse.relocate") \
    X(ParseGeometry, "parse.geometry") \
    X(SourceReadDecodeHash, "source.read_decode_hash") \
    X(SemanticSnapshot, "semantic.snapshot") \
    X(SourcePatchReadDecodeHash, "source.patch_read_decode_hash") \
    X(PlanDistanceIndex, "plan.distance_index") \
    X(ReportSnapshot, "report.snapshot") \
    X(TransactionTotal, "transaction.total") \
    X(TransactionStageVerify, "transaction.stage_verify") \
    X(TransactionConcurrencyCheck, "transaction.concurrency_check") \
    X(TransactionReplaceVerify, "transaction.replace_verify") \
    X(TransactionRollback, "transaction.rollback") \
    X(CandidateReparse, "candidate.reparse") \
    X(ValidationReparse, "validation.reparse") \
    X(ValidationBeforeSnapshot, "validation.before_snapshot") \
    X(ValidationAfterSnapshot, "validation.after_snapshot") \
    X(ValidationTargets, "validation.targets") \
    X(ValidationNonTargets, "validation.non_targets") \
    X(ValidationIdentitiesLinks, "validation.identities_links") \
    X(ValidationDerivedVariables, "validation.derived_variables") \
    X(ContextReplaceDestroy, "context.replace_destroy") \
    X(PlanPrepare, "plan.prepare") \
    X(PlanPatchSources, "plan.patch_sources") \
    X(PlanValidate, "plan.validate") \
    X(ResetReparse, "reset.reparse") \
    X(ResetDiskIdentityCheck, "reset.disk_identity_check") \
    X(SavePrevalidate, "save.prevalidate") \
    X(SaveCandidateDestroy, "save.candidate_destroy") \
    X(SaveReadHashEncode, "save.read_hash_encode") \
    X(SaveTransactionPrepare, "save.transaction_prepare") \
    X(SaveTransaction, "save.transaction") \
    X(SaveCommittedMetadata, "save.committed_metadata") \
    X(BatchConvert, "batch.convert")
enum class MapStage { KME_MAP_TIMING_STAGES(KME_STAGE_ENUM) Count };
inline constexpr std::array k_map_stage_names{KME_MAP_TIMING_STAGES(KME_STAGE_NAME)};
#undef KME_MAP_TIMING_STAGES

#undef KME_STAGE_ENUM
#undef KME_STAGE_NAME

template<class Key, const auto& Names>
class Timing {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t stage_count = static_cast<std::size_t>(Key::Count);
    static_assert(Names.size() == stage_count);

    struct Trace {
        void stop() noexcept { if (!end_) end_ = Clock::now(); }

        double seconds() const noexcept {
            return std::chrono::duration<double>((end_ ? *end_ : Clock::now()) - start_).count();
        }

        std::uint64_t count(std::string_view name) const noexcept {
            for (std::size_t i = 0; i < stage_count; ++i) {
                if (name == Names[i]) return samples_[i].count;
            }
            return 0;
        }

        void add(Key key, double seconds) noexcept {
            const auto index = static_cast<std::size_t>(key);
            auto& sample = samples_[index];
            if (sample.count++ == 0) order_[used_++] = index;
            sample.seconds += seconds;
        }

        std::string format(std::string_view operation, double total,
                           std::string_view outcome) const {
            std::ostringstream out;
            out << std::fixed << std::setprecision(3)
                << "edit timing: " << operation << " outcome=" << outcome
                << " total_ms=" << total * 1000.0 << " stages=inclusive";
            for (std::size_t i = 0; i < used_; ++i) {
                const auto index = order_[i];
                const auto& sample = samples_[index];
                out << " " << Names[index] << "_ms=" << sample.seconds * 1000.0
                    << " " << Names[index] << "_count=" << sample.count;
            }
            return out.str();
        }

    private:
        struct Sample {
            double seconds = 0.0;
            std::uint64_t count = 0;
        };
        Clock::time_point start_ = Clock::now();
        std::optional<Clock::time_point> end_;
        std::array<Sample, stage_count> samples_{};
        std::array<std::size_t, stage_count> order_{};
        std::size_t used_ = 0;
    };

    class Activation {
    public:
        explicit Activation(Trace* trace) noexcept : previous_(active_) { active_ = trace; }
        ~Activation() { reset(); }
        Activation(const Activation&) = delete;
        Activation& operator=(const Activation&) = delete;
        void reset() noexcept {
            if (reset_) return;
            active_ = previous_;
            reset_ = true;
        }
    private:
        Trace* previous_;
        bool reset_ = false;
    };

    class Stage {
    public:
        explicit Stage(Key key) noexcept : trace_(active_), key_(key) {
            if (trace_) start_ = Clock::now();
        }
        ~Stage() { finish(); }
        void next(Key key) noexcept {
            finish();
            key_ = key;
            if (trace_) start_ = Clock::now();
        }
        void finish() noexcept {
            if (trace_ && key_ != Key::Count) {
                trace_->add(key_, std::chrono::duration<double>(Clock::now() - start_).count());
                key_ = Key::Count;
            }
        }
        Stage(const Stage&) = delete;
        Stage& operator=(const Stage&) = delete;
    private:
        Trace* trace_;
        Key key_;
        Clock::time_point start_{};
    };

private:
    inline static thread_local Trace* active_ = nullptr;
};

using GuiTiming = Timing<GuiStage, k_gui_stage_names>;
using MapTiming = Timing<MapStage, k_map_stage_names>;

} // namespace kme::timing
