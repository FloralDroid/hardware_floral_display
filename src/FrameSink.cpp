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

#include "floral/display/FrameSink.h"

#include <mutex>
#include <utility>

namespace floral::display {
namespace {

class PassthroughFrameSink final : public FrameSink {
  public:
    const char* Name() const override { return "passthrough"; }

    FrameSinkResult Submit(FrameSubmission submission) override {
        const bool hasAcquireFence = submission.acquire_fence.ok();
        FrameSinkResult result;
        result.present_fence = std::move(submission.acquire_fence);

        std::lock_guard lock(mutex_);
        ++stats_.submitted_frames;
        if (submission.buffer != nullptr) {
            ++stats_.frames_with_buffer;
        }
        if (hasAcquireFence) {
            ++stats_.frames_with_acquire_fence;
            ++stats_.returned_present_fences;
        }
        stats_.last_sequence = submission.sequence;
        stats_.last_submission_time_nanos = submission.submission_time_nanos;
        stats_.last_dataspace = submission.dataspace;
        stats_.last_damage_rect_count = static_cast<uint32_t>(submission.damage.size());
        return result;
    }

    FrameSinkStats GetStats() const override {
        std::lock_guard lock(mutex_);
        return stats_;
    }

  private:
    mutable std::mutex mutex_;
    FrameSinkStats stats_;
};

}  // namespace

std::unique_ptr<FrameSink> CreatePassthroughFrameSink() {
    return std::make_unique<PassthroughFrameSink>();
}

}  // namespace floral::display
