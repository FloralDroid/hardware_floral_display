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
    : topology_(std::move(primaryConfig)) {
    for (DisplayConfig config : topology_.ConnectedDisplays()) {
        const hwc2_display_t id = static_cast<hwc2_display_t>(config.id);
        std::unique_ptr<FrameSink> frameSink =
                frameSinkFactory ? frameSinkFactory(config) : CreatePassthroughFrameSink();
        displays_.emplace(id, std::make_unique<Display>(std::move(config), vsyncCallback,
                                                        std::move(frameSink)));
    }
}

Display* DisplayRegistry::Get(hwc2_display_t id) const {
    std::lock_guard lock(mutex_);
    const auto found = displays_.find(id);
    return found == displays_.end() ? nullptr : found->second.get();
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

}  // namespace floral::display
