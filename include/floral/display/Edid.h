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

#include <array>
#include <cstdint>
#include <string_view>

namespace floral::display {

constexpr size_t kEdidBlockSize = 128;
using EdidBlock = std::array<uint8_t, kEdidBlockSize>;

// Builds a stable EDID 1.4 identity. Display modes continue to come from HWC
// configs; the EDID identifies the connector and product to SurfaceFlinger.
EdidBlock BuildEdid(uint8_t port, std::string_view displayName);

bool HasValidEdidChecksum(const EdidBlock& edid);

}  // namespace floral::display
