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
#include <string>
#include <unordered_map>

#include "floral/display/DisplayRegistry.h"

namespace floral::display {

class HwcDevice : public hwc2_device_t {
  public:
    HwcDevice();
    ~HwcDevice();

    HwcDevice(const HwcDevice&) = delete;
    HwcDevice& operator=(const HwcDevice&) = delete;

    static HwcDevice* From(hwc2_device_t* device) { return static_cast<HwcDevice*>(device); }

    Display* GetDisplay(hwc2_display_t id) const;
    void GetCapabilities(uint32_t* outCount, int32_t* outCapabilities);
    hwc2_function_pointer_t GetFunction(int32_t descriptor);
    int32_t RegisterCallback(int32_t descriptor, hwc2_callback_data_t callbackData,
                             hwc2_function_pointer_t pointer);
    std::string Dump() const;

  private:
    struct CallbackEntry {
        hwc2_callback_data_t data = nullptr;
        hwc2_function_pointer_t pointer = nullptr;
    };

    static DisplayConfig LoadPrimaryDisplayConfig();
    void EmitVsync(hwc2_display_t display, int64_t timestamp, int64_t periodNanos);

    mutable std::mutex callback_mutex_;
    std::unordered_map<int32_t, CallbackEntry> callbacks_;
    std::unique_ptr<DisplayRegistry> registry_;
};

}  // namespace floral::display
