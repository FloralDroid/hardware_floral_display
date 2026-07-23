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

#include "floral/display/DisplayTopology.h"

#include <algorithm>
#include <utility>

namespace floral::display {

DisplayTopology::DisplayTopology(DisplayConfig primaryConfig) {
    // The permanent internal display owns the stable primary identity. Future
    // hotplug displays must never replace or renumber this entry.
    primaryConfig.id = kPrimaryDisplayId;
    primaryConfig.port = 0;
    primaryConfig.connection_type = ConnectionType::kInternal;
    displays_.emplace(kPrimaryDisplayId, std::move(primaryConfig));
}

std::optional<DisplayConfig> DisplayTopology::Get(DisplayId id) const {
    std::lock_guard lock(mutex_);
    const auto found = displays_.find(id);
    if (found == displays_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<DisplayConfig> DisplayTopology::ConnectedDisplays() const {
    std::lock_guard lock(mutex_);
    std::vector<DisplayConfig> displays;
    displays.reserve(displays_.size());
    for (const auto& [id, config] : displays_) {
        (void)id;
        displays.push_back(config);
    }
    std::sort(displays.begin(), displays.end(),
              [](const DisplayConfig& left, const DisplayConfig& right) {
                  return left.id < right.id;
              });
    return displays;
}

}  // namespace floral::display
