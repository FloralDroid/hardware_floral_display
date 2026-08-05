/*
 * Copyright 2026 FloralDroid
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "floral/display/DisplayConfig.h"

#include <algorithm>

namespace floral::display {

int64_t RefreshRateToVsyncPeriodNanos(uint32_t refreshRateHz) {
    if (refreshRateHz == 0) {
        return 0;
    }
    return (1'000'000'000LL + refreshRateHz / 2) / refreshRateHz;
}

std::vector<uint32_t> LimitRefreshRates(const std::vector<uint32_t>& supportedRefreshRatesHz,
                                        uint32_t maximumRefreshRateHz) {
    std::vector<uint32_t> limited;
    if (maximumRefreshRateHz == 0) {
        return limited;
    }
    for (const uint32_t refreshRateHz : supportedRefreshRatesHz) {
        if (refreshRateHz > 0 && refreshRateHz <= maximumRefreshRateHz) {
            limited.push_back(refreshRateHz);
        }
    }
    // Preserve an exact mode for nonstandard caps such as 24 or 45 Hz while
    // keeping every advertised SurfaceFlinger choice at or below the limit.
    limited.push_back(maximumRefreshRateHz);
    std::sort(limited.begin(), limited.end());
    limited.erase(std::unique(limited.begin(), limited.end()), limited.end());
    return limited;
}

}  // namespace floral::display
