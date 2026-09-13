/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3d_types.h"
#include <atomic>

namespace canvas3d_detail {

PutBetweenSourceTemplate prepare_put_between_source(const CpuModelData& source);
CpuModelData derive_put_between_model(const CpuModelData& source,
                                      const PutBetweenSourceTemplate& source_template,
                                      const SceneModelLoadRequest& request);
#ifndef NDEBUG
extern std::atomic<int> g_debug_put_between_derive_throw_countdown;
extern std::atomic<size_t> g_debug_put_between_prepare_count;
#endif

} // namespace canvas3d_detail
