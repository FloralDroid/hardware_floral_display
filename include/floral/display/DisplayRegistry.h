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

#include <hardware/hwcomposer2.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "floral/display/Display.h"
#include "floral/display/DisplayTopology.h"

namespace floral::display {

class DisplayRegistry {
  public:
    static constexpr hwc2_display_t kPrimaryDisplayId =
            static_cast<hwc2_display_t>(DisplayTopology::kPrimaryDisplayId);

    DisplayRegistry(DisplayConfig primaryConfig, Display::VsyncCallback vsyncCallback);
    ~DisplayRegistry() = default;

    DisplayRegistry(const DisplayRegistry&) = delete;
    DisplayRegistry& operator=(const DisplayRegistry&) = delete;

    Display* Get(hwc2_display_t id) const;
    std::vector<hwc2_display_t> ConnectedDisplayIds() const;

  private:
    DisplayTopology topology_;
    mutable std::mutex mutex_;
    std::unordered_map<hwc2_display_t, std::unique_ptr<Display>> displays_;
};

}  // namespace floral::display
