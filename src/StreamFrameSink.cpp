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

#define LOG_TAG "hwcomposer.floral"

#include "floral/display/StreamFrameSink.h"

#include <fcntl.h>
#include <inttypes.h>
#include <log/log.h>

#include <mutex>
#include <unordered_map>
#include <utility>

namespace floral::display {
namespace {

android::base::unique_fd DuplicateFence(const android::base::unique_fd& fence) {
    if (!fence.ok()) {
        return {};
    }
    return android::base::unique_fd(fcntl(fence.get(), F_DUPFD_CLOEXEC, 0));
}

bool ShouldLogFailure(uint64_t failureCount) {
    return failureCount != 0 && (failureCount & (failureCount - 1)) == 0;
}

class StreamFrameSink final : public FrameSink {
  public:
    StreamFrameSink(std::shared_ptr<FrameConsumerEndpoint> consumer,
                    std::shared_ptr<ClientTargetResolver> targetResolver)
        : consumer_(std::move(consumer)), target_resolver_(std::move(targetResolver)) {}

    const char* Name() const override { return "stream"; }

    FrameSinkResult Submit(FrameSubmission submission) override {
        const bool hadAcquireFence = submission.acquire_fence.ok();
        const FrameConsumerStreamState streamState =
                consumer_->GetStreamState(submission.display_id);
        if (!streamState.accepting_frames || submission.buffer == nullptr) {
            if (!streamState.accepting_frames) {
                ResetRegisteredBuffers();
            }
            return FinishSubmission(submission, hadAcquireFence,
                                    std::move(submission.acquire_fence));
        }

        ResolvedClientTarget target;
        if (!target_resolver_->Resolve(submission.buffer, submission.width, submission.height,
                                       &target)) {
            const uint64_t failures = RecordFailure(&FrameSinkStats::resolver_failures);
            if (ShouldLogFailure(failures)) {
                ALOGW("Client target resolution failed: display=%" PRIu64
                      " sequence=%" PRIu64 " failures=%" PRIu64,
                      submission.display_id, submission.sequence, failures);
            }
            return FinishSubmission(submission, hadAcquireFence,
                                    std::move(submission.acquire_fence));
        }
        if (target.descriptor.protected_content) {
            return FinishSubmission(submission, hadAcquireFence,
                                    std::move(submission.acquire_fence));
        }

        uint64_t bufferId = 0;
        if (!EnsureBufferRegistered(submission, streamState.generation, target, &bufferId)) {
            return FinishSubmission(submission, hadAcquireFence,
                                    std::move(submission.acquire_fence));
        }

        android::base::unique_fd consumerAcquireFence = DuplicateFence(submission.acquire_fence);
        if (submission.acquire_fence.ok() && !consumerAcquireFence.ok()) {
            return FinishSubmission(submission, hadAcquireFence,
                                    std::move(submission.acquire_fence));
        }

        StreamFrameRequest request;
        request.display_id = submission.display_id;
        request.generation = streamState.generation;
        request.buffer_id = bufferId;
        request.source_sequence = submission.sequence;
        request.frame_submit_time_nanos = submission.submission_time_nanos;
        request.presentation_time_nanos = submission.submission_time_nanos;
        request.dataspace = submission.dataspace;
        request.acquire_fence = std::move(consumerAcquireFence);

        StreamFrameResult consumerResult = consumer_->SubmitFrame(std::move(request));
        if (consumerResult.status != FrameConsumerStatus::kAccepted) {
            const uint64_t failures = RecordFailure(&FrameSinkStats::submit_failures);
            if (ShouldLogFailure(failures)) {
                ALOGW("Frame submission failed: display=%" PRIu64
                      " sequence=%" PRIu64 " status=%d failures=%" PRIu64,
                      submission.display_id, submission.sequence,
                      static_cast<int32_t>(consumerResult.status), failures);
            }
            InvalidateRegistration(target.identity, bufferId, consumerResult.status);
            return FinishSubmission(submission, hadAcquireFence,
                                    std::move(submission.acquire_fence));
        }

        submission.acquire_fence.reset();
        return FinishSubmission(submission, hadAcquireFence,
                                std::move(consumerResult.release_fence));
    }

    FrameSinkStats GetStats() const override {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

  private:
    bool EnsureBufferRegistered(const FrameSubmission& submission, uint32_t generation,
                                const ResolvedClientTarget& target, uint64_t* outBufferId) {
        std::lock_guard lock(registry_mutex_);
        if (!has_generation_ || generation != active_generation_) {
            registered_buffers_.clear();
            active_generation_ = generation;
            has_generation_ = true;
        }

        const auto found = registered_buffers_.find(target.identity);
        if (found != registered_buffers_.end()) {
            *outBufferId = found->second;
            return true;
        }

        StreamBufferRegistration registration;
        registration.display_id = submission.display_id;
        registration.generation = generation;
        registration.buffer_id = next_buffer_id_;
        registration.buffer = submission.buffer;
        registration.descriptor = target.descriptor;
        const FrameConsumerStatus status = consumer_->RegisterBuffer(registration);
        if (status != FrameConsumerStatus::kAccepted) {
            const uint64_t failures = RecordFailure(&FrameSinkStats::register_failures);
            if (ShouldLogFailure(failures)) {
                ALOGW("Buffer registration failed: display=%" PRIu64
                      " generation=%u buffer=%" PRIu64 " status=%d failures=%" PRIu64,
                      submission.display_id, generation, next_buffer_id_,
                      static_cast<int32_t>(status), failures);
            }
            return false;
        }

        *outBufferId = next_buffer_id_++;
        registered_buffers_.emplace(target.identity, *outBufferId);
        return true;
    }

    void ResetRegisteredBuffers() {
        std::lock_guard lock(registry_mutex_);
        registered_buffers_.clear();
        has_generation_ = false;
    }

    void InvalidateRegistration(uint64_t identity, uint64_t bufferId, FrameConsumerStatus status) {
        std::lock_guard lock(registry_mutex_);
        if (status == FrameConsumerStatus::kStaleGeneration ||
            status == FrameConsumerStatus::kNoActiveStream) {
            registered_buffers_.clear();
            has_generation_ = false;
            return;
        }
        if (status != FrameConsumerStatus::kBufferUnknown) {
            return;
        }

        const auto found = registered_buffers_.find(identity);
        if (found != registered_buffers_.end() && found->second == bufferId) {
            registered_buffers_.erase(found);
        }
    }

    uint64_t RecordFailure(uint64_t FrameSinkStats::*counter) {
        std::lock_guard lock(stats_mutex_);
        return ++(stats_.*counter);
    }

    FrameSinkResult FinishSubmission(const FrameSubmission& submission, bool hadAcquireFence,
                                     android::base::unique_fd presentFence) {
        FrameSinkResult result;
        result.present_fence = std::move(presentFence);

        std::lock_guard lock(stats_mutex_);
        ++stats_.submitted_frames;
        if (submission.buffer != nullptr) {
            ++stats_.frames_with_buffer;
        }
        if (hadAcquireFence) {
            ++stats_.frames_with_acquire_fence;
        }
        if (result.present_fence.ok()) {
            ++stats_.returned_present_fences;
        }
        stats_.last_sequence = submission.sequence;
        stats_.last_submission_time_nanos = submission.submission_time_nanos;
        stats_.last_dataspace = submission.dataspace;
        stats_.last_damage_rect_count = static_cast<uint32_t>(submission.damage.size());
        return result;
    }

    // Connection and allocator-specific metadata providers.
    const std::shared_ptr<FrameConsumerEndpoint> consumer_;
    const std::shared_ptr<ClientTargetResolver> target_resolver_;

    // Generation-scoped buffer registration cache.
    std::mutex registry_mutex_;
    bool has_generation_ = false;
    uint32_t active_generation_ = 0;
    uint64_t next_buffer_id_ = 1;
    std::unordered_map<uint64_t, uint64_t> registered_buffers_;

    // Presentation diagnostics.
    mutable std::mutex stats_mutex_;
    FrameSinkStats stats_;
};

}  // namespace

std::unique_ptr<FrameSink> CreateStreamFrameSink(
        std::shared_ptr<FrameConsumerEndpoint> consumer,
        std::shared_ptr<ClientTargetResolver> targetResolver) {
    if (consumer == nullptr || targetResolver == nullptr) {
        return nullptr;
    }
    return std::make_unique<StreamFrameSink>(std::move(consumer), std::move(targetResolver));
}

}  // namespace floral::display
