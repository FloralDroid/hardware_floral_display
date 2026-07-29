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

#include "floral/display/FrameSink.h"

namespace floral::display {

enum class FrameConsumerStatus : int32_t {
    kAccepted = 0,
    kDropped = 1,
    kNoActiveStream = 2,
    kStaleGeneration = 3,
    kBufferUnknown = 4,
    kUnsupportedBuffer = 5,
    kInternalError = 6,
};

struct FrameConsumerStreamState {
    uint32_t generation = 0;
    bool accepting_frames = false;
};

struct ClientTargetDescriptor {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    int32_t format = 0;
    uint64_t usage = 0;
    uint32_t stride = 0;
    bool protected_content = false;
};

struct ResolvedClientTarget {
    // Stable and collision-free for the lifetime of one endpoint generation.
    uint64_t identity = 0;
    ClientTargetDescriptor descriptor;
};

struct StreamBufferRegistration {
    DisplayId display_id = 0;
    uint32_t generation = 0;
    uint64_t buffer_id = 0;
    const native_handle_t* buffer = nullptr;
    ClientTargetDescriptor descriptor;
};

struct StreamFrameRequest {
    DisplayId display_id = 0;
    uint32_t generation = 0;
    uint64_t buffer_id = 0;
    uint64_t source_sequence = 0;
    int64_t frame_submit_time_nanos = 0;
    int64_t presentation_time_nanos = 0;
    int32_t dataspace = 0;
    android::base::unique_fd acquire_fence;
};

struct StreamFrameResult {
    FrameConsumerStatus status = FrameConsumerStatus::kInternalError;
    android::base::unique_fd release_fence;
};

// Resolves allocator-specific client target metadata without exposing a
// gralloc handle layout to the FrameSink implementation.
class ClientTargetResolver {
  public:
    virtual ~ClientTargetResolver() = default;

    virtual bool Resolve(const native_handle_t* buffer, uint32_t displayWidth,
                         uint32_t displayHeight, ResolvedClientTarget* outTarget) = 0;
};

// Implementations must copy the borrowed native handle during RegisterBuffer.
// An accepted frame without a release fence means source access completed
// synchronously. GetStreamState must return cached state on the present path.
class FrameConsumerEndpoint {
  public:
    virtual ~FrameConsumerEndpoint() = default;

    virtual FrameConsumerStreamState GetStreamState(DisplayId displayId) = 0;
    virtual FrameConsumerStatus RegisterBuffer(const StreamBufferRegistration& registration) = 0;
    virtual StreamFrameResult SubmitFrame(StreamFrameRequest request) = 0;
};

std::unique_ptr<FrameSink> CreateStreamFrameSink(
        std::shared_ptr<FrameConsumerEndpoint> consumer,
        std::shared_ptr<ClientTargetResolver> targetResolver);

}  // namespace floral::display
