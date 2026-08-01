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

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "floral/display/DisplayConfig.h"

namespace floral::display {

enum class DisplayTopologyResult : uint8_t {
    kSuccess,
    kInvalidConfig,
    kAlreadyConnected,
    kPortInUse,
    kPrimaryDisplayProtected,
    kNotFound,
};

struct DisplayTopologyChanges {
    std::vector<DisplayId> disconnected_display_ids;
    std::vector<DisplayConfig> connected_displays;
};

class DisplayTopology {
  public:
    static constexpr DisplayId kPrimaryDisplayId = 1;

    explicit DisplayTopology(DisplayConfig primaryConfig);

    std::optional<DisplayConfig> Get(DisplayId id) const;
    DisplayTopologyResult ConnectExternal(DisplayConfig config);
    DisplayTopologyResult Disconnect(DisplayId id);
    DisplayTopologyResult ReplaceExternalDisplays(std::vector<DisplayConfig> configs,
                                                  DisplayTopologyChanges* outChanges);
    std::vector<DisplayConfig> ConnectedDisplays() const;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<DisplayId, DisplayConfig> displays_;
};

}  // namespace floral::display
