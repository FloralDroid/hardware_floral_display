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
#include <cutils/native_handle.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "floral/display/DisplayConfig.h"

namespace floral::display {

struct DamageRect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

struct FrameSubmission {
    DisplayId display_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    const native_handle_t* buffer = nullptr;
    android::base::unique_fd acquire_fence;
    int32_t dataspace = 0;
    std::vector<DamageRect> damage;
    uint64_t sequence = 0;
    int64_t submission_time_nanos = 0;
};

struct FrameSinkResult {
    android::base::unique_fd present_fence;
};

struct FrameSinkStats {
    uint64_t submitted_frames = 0;
    uint64_t frames_with_buffer = 0;
    uint64_t frames_with_acquire_fence = 0;
    uint64_t returned_present_fences = 0;
    uint64_t last_sequence = 0;
    int64_t last_submission_time_nanos = 0;
    int32_t last_dataspace = 0;
    uint32_t last_damage_rect_count = 0;
};

// A sink owns the acquire fence passed to Submit. The client target remains
// borrowed until the returned present fence signals.
class FrameSink {
  public:
    virtual ~FrameSink() = default;

    virtual const char* Name() const = 0;
    virtual FrameSinkResult Submit(FrameSubmission submission) = 0;
    virtual FrameSinkStats GetStats() const = 0;
};

std::unique_ptr<FrameSink> CreatePassthroughFrameSink();

}  // namespace floral::display
