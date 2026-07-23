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

#include <android-base/unique_fd.h>
#include <hardware/hwcomposer2.h>

#include <cstdint>

namespace floral::display {

class Layer {
  public:
    explicit Layer(hwc2_layer_t id) : id_(id) {}

    hwc2_layer_t id() const { return id_; }
    int32_t composition_type() const { return composition_type_; }
    uint32_t z_order() const { return z_order_; }

    void SetCompositionType(int32_t type) { composition_type_ = type; }
    void SetZOrder(uint32_t z) { z_order_ = z; }
    void SetBuffer(buffer_handle_t buffer, int32_t acquireFence);
    void ReleaseBuffer();

  private:
    hwc2_layer_t id_;
    int32_t composition_type_ = HWC2_COMPOSITION_INVALID;
    uint32_t z_order_ = 0;
    buffer_handle_t buffer_ = nullptr;  // Buffer ownership remains with gralloc.
    android::base::unique_fd acquire_fence_;
};

}  // namespace floral::display
