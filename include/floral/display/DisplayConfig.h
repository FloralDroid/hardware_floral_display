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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace floral::display {

using DisplayId = uint64_t;

enum class ConnectionType : uint8_t {
    kInternal,
    kExternal,
};

int64_t RefreshRateToVsyncPeriodNanos(uint32_t refreshRateHz);
std::vector<uint32_t> LimitRefreshRates(const std::vector<uint32_t>& supportedRefreshRatesHz,
                                        uint32_t maximumRefreshRateHz);

// This descriptor is independent of HIDL and AIDL Composer types so each
// Android-version frontend can consume the same display identity and modes.
struct DisplayConfig {
    DisplayId id = 1;
    uint8_t port = 0;
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t dpi = 320;
    int64_t vsync_period_nanos = 16'666'667;
    std::vector<uint32_t> supported_refresh_rates_hz;
    ConnectionType connection_type = ConnectionType::kInternal;
    std::string name = "Floral Internal Display";
};

}  // namespace floral::display
