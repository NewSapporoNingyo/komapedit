/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <cmath>
#include <limits>

namespace kme {

inline int truncating_int_or_zero(double value) noexcept {
    if (!std::isfinite(value)) return 0;
    const double truncated = std::trunc(value);
    if (truncated < static_cast<double>(std::numeric_limits<int>::min()) ||
        truncated > static_cast<double>(std::numeric_limits<int>::max())) {
        return 0;
    }
    return static_cast<int>(truncated);
}

} // namespace kme
