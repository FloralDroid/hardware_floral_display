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

#include "floral/display/DisplayRegistry.h"

#include <algorithm>
#include <utility>

namespace floral::display {

DisplayRegistry::DisplayRegistry(DisplayConfig primaryConfig, Display::VsyncCallback vsyncCallback,
                                 FrameSinkFactory frameSinkFactory)
    : topology_(std::move(primaryConfig)),
      vsync_callback_(std::move(vsyncCallback)),
      frame_sink_factory_(std::move(frameSinkFactory)) {
    for (DisplayConfig config : topology_.ConnectedDisplays()) {
        const hwc2_display_t id = static_cast<hwc2_display_t>(config.id);
        displays_.emplace(id, CreateDisplay(std::move(config)));
    }
}

std::shared_ptr<Display> DisplayRegistry::Get(hwc2_display_t id) const {
    std::lock_guard lock(mutex_);
    const auto found = displays_.find(id);
    return found == displays_.end() ? nullptr : found->second;
}

void DisplayRegistry::SetHotplugCallback(HotplugCallback hotplugCallback) {
    std::lock_guard hotplugLock(hotplug_mutex_);
    hotplug_callback_ = std::move(hotplugCallback);
    if (!hotplug_callback_) {
        return;
    }

    // Installing the callback and publishing the initial snapshot share the
    // same ordering boundary as later topology mutations.
    for (const hwc2_display_t id : ConnectedDisplayIds()) {
        hotplug_callback_(id, true);
    }
}

DisplayTopologyResult DisplayRegistry::ConnectExternal(DisplayConfig config) {
    std::lock_guard hotplugLock(hotplug_mutex_);
    const DisplayConfig displayConfig = config;
    const DisplayTopologyResult result = topology_.ConnectExternal(std::move(config));
    if (result != DisplayTopologyResult::kSuccess) {
        return result;
    }

    const hwc2_display_t id = static_cast<hwc2_display_t>(displayConfig.id);
    std::shared_ptr<Display> display = CreateDisplay(displayConfig);
    {
        std::lock_guard lock(mutex_);
        displays_.emplace(id, std::move(display));
    }
    if (hotplug_callback_) {
        hotplug_callback_(id, true);
    }
    return DisplayTopologyResult::kSuccess;
}

DisplayTopologyResult DisplayRegistry::Disconnect(hwc2_display_t id) {
    std::lock_guard hotplugLock(hotplug_mutex_);
    const DisplayTopologyResult result = topology_.Disconnect(static_cast<DisplayId>(id));
    if (result != DisplayTopologyResult::kSuccess) {
        return result;
    }

    {
        std::lock_guard lock(mutex_);
        displays_.erase(id);
    }
    if (hotplug_callback_) {
        hotplug_callback_(id, false);
    }
    return DisplayTopologyResult::kSuccess;
}

DisplayTopologyResult DisplayRegistry::ReplaceExternalDisplays(std::vector<DisplayConfig> configs) {
    std::lock_guard hotplugLock(hotplug_mutex_);
    DisplayTopologyChanges changes;
    const DisplayTopologyResult result =
            topology_.ReplaceExternalDisplays(std::move(configs), &changes);
    if (result != DisplayTopologyResult::kSuccess) {
        return result;
    }

    std::vector<std::pair<hwc2_display_t, std::shared_ptr<Display>>> connectedDisplays;
    connectedDisplays.reserve(changes.connected_displays.size());
    for (DisplayConfig& config : changes.connected_displays) {
        const hwc2_display_t id = static_cast<hwc2_display_t>(config.id);
        connectedDisplays.emplace_back(id, CreateDisplay(std::move(config)));
    }

    for (DisplayId id : changes.disconnected_display_ids) {
        {
            std::lock_guard lock(mutex_);
            displays_.erase(static_cast<hwc2_display_t>(id));
        }
        if (hotplug_callback_) {
            hotplug_callback_(static_cast<hwc2_display_t>(id), false);
        }
    }
    for (auto& [id, display] : connectedDisplays) {
        {
            std::lock_guard lock(mutex_);
            displays_.insert_or_assign(id, std::move(display));
        }
        if (hotplug_callback_) {
            hotplug_callback_(id, true);
        }
    }
    return DisplayTopologyResult::kSuccess;
}

std::vector<hwc2_display_t> DisplayRegistry::ConnectedDisplayIds() const {
    std::lock_guard lock(mutex_);
    std::vector<hwc2_display_t> ids;
    ids.reserve(displays_.size());
    for (const auto& [id, display] : displays_) {
        (void)display;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::shared_ptr<Display> DisplayRegistry::CreateDisplay(DisplayConfig config) const {
    std::unique_ptr<FrameSink> frameSink =
            frame_sink_factory_ ? frame_sink_factory_(config) : CreatePassthroughFrameSink();
    if (frameSink == nullptr) {
        frameSink = CreatePassthroughFrameSink();
    }
    return std::make_shared<Display>(std::move(config), vsync_callback_, std::move(frameSink));
}

}  // namespace floral::display
