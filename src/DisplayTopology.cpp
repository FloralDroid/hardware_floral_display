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
#include <unordered_set>
#include <utility>

namespace floral::display {
namespace {

bool IsValidExternalConfig(const DisplayConfig& config) {
    return config.id != 0 && config.port != 0 && config.width != 0 && config.height != 0 &&
           config.dpi != 0 && config.vsync_period_nanos > 0;
}

bool SameDisplayConfig(const DisplayConfig& left, const DisplayConfig& right) {
    return left.id == right.id && left.port == right.port && left.width == right.width &&
           left.height == right.height && left.dpi == right.dpi &&
           left.vsync_period_nanos == right.vsync_period_nanos &&
           left.supported_refresh_rates_hz == right.supported_refresh_rates_hz &&
           left.connection_type == right.connection_type && left.name == right.name;
}

}  // namespace

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

DisplayTopologyResult DisplayTopology::ConnectExternal(DisplayConfig config) {
    if (config.id == kPrimaryDisplayId) {
        return DisplayTopologyResult::kPrimaryDisplayProtected;
    }
    if (!IsValidExternalConfig(config)) {
        return DisplayTopologyResult::kInvalidConfig;
    }

    std::lock_guard lock(mutex_);
    if (displays_.find(config.id) != displays_.end()) {
        return DisplayTopologyResult::kAlreadyConnected;
    }
    const auto portInUse =
            std::find_if(displays_.begin(), displays_.end(),
                         [&config](const auto& entry) { return entry.second.port == config.port; });
    if (portInUse != displays_.end()) {
        return DisplayTopologyResult::kPortInUse;
    }

    config.connection_type = ConnectionType::kExternal;
    displays_.emplace(config.id, std::move(config));
    return DisplayTopologyResult::kSuccess;
}

DisplayTopologyResult DisplayTopology::Disconnect(DisplayId id) {
    if (id == kPrimaryDisplayId) {
        return DisplayTopologyResult::kPrimaryDisplayProtected;
    }
    if (id == 0) {
        return DisplayTopologyResult::kNotFound;
    }

    std::lock_guard lock(mutex_);
    const auto found = displays_.find(id);
    if (found == displays_.end()) {
        return DisplayTopologyResult::kNotFound;
    }
    displays_.erase(found);
    return DisplayTopologyResult::kSuccess;
}

DisplayTopologyResult DisplayTopology::ReplaceExternalDisplays(std::vector<DisplayConfig> configs,
                                                               DisplayTopologyChanges* outChanges) {
    if (outChanges == nullptr) {
        return DisplayTopologyResult::kInvalidConfig;
    }
    *outChanges = {};

    std::sort(configs.begin(), configs.end(),
              [](const DisplayConfig& left, const DisplayConfig& right) {
                  return left.id < right.id;
              });
    std::unordered_set<DisplayId> displayIds;
    std::unordered_set<uint8_t> ports;
    for (DisplayConfig& config : configs) {
        if (config.id == kPrimaryDisplayId) {
            return DisplayTopologyResult::kPrimaryDisplayProtected;
        }
        if (!IsValidExternalConfig(config)) {
            return DisplayTopologyResult::kInvalidConfig;
        }
        if (!displayIds.insert(config.id).second) {
            return DisplayTopologyResult::kAlreadyConnected;
        }
        if (!ports.insert(config.port).second) {
            return DisplayTopologyResult::kPortInUse;
        }
        config.connection_type = ConnectionType::kExternal;
    }

    DisplayTopologyChanges changes;
    std::lock_guard lock(mutex_);
    std::unordered_map<DisplayId, DisplayConfig> targetDisplays;
    targetDisplays.reserve(configs.size() + 1);
    targetDisplays.emplace(kPrimaryDisplayId, displays_.at(kPrimaryDisplayId));
    for (const DisplayConfig& config : configs) {
        targetDisplays.emplace(config.id, config);
    }

    for (const auto& [id, current] : displays_) {
        if (id == kPrimaryDisplayId) {
            continue;
        }
        const auto target = targetDisplays.find(id);
        if (target == targetDisplays.end() || !SameDisplayConfig(current, target->second)) {
            changes.disconnected_display_ids.push_back(id);
        }
    }
    for (const DisplayConfig& config : configs) {
        const auto current = displays_.find(config.id);
        if (current == displays_.end() || !SameDisplayConfig(current->second, config)) {
            changes.connected_displays.push_back(config);
        }
    }

    std::sort(changes.disconnected_display_ids.begin(), changes.disconnected_display_ids.end());
    displays_ = std::move(targetDisplays);
    *outChanges = std::move(changes);
    return DisplayTopologyResult::kSuccess;
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
