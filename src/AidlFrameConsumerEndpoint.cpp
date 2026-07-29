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

#include "floral/display/AidlFrameConsumerEndpoint.h"

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidl/floral/stream/display/BufferRegistration.h>
#include <aidl/floral/stream/display/FrameRequest.h>
#include <aidl/floral/stream/display/FrameResult.h>
#include <aidl/floral/stream/display/FrameStatus.h>
#include <aidl/floral/stream/display/StreamState.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android/binder_manager.h>

#include <fcntl.h>

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace floral::display {
namespace {

using AidlBufferUsage = aidl::android::hardware::graphics::common::BufferUsage;
using AidlFrameConsumer = aidl::floral::stream::display::IFrameConsumer;
using AidlFrameStatus = aidl::floral::stream::display::FrameStatus;
using AidlPixelFormat = aidl::android::hardware::graphics::common::PixelFormat;

bool FitsInt32(uint32_t value) {
    return value <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
}

bool FitsInt64(uint64_t value) {
    return value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

android::base::unique_fd DuplicateFence(const ndk::ScopedFileDescriptor& fence) {
    if (fence.get() < 0) {
        return {};
    }
    return android::base::unique_fd(fcntl(fence.get(), F_DUPFD_CLOEXEC, 0));
}

FrameConsumerStatus FromAidlStatus(AidlFrameStatus status) {
    switch (status) {
        case AidlFrameStatus::ACCEPTED:
            return FrameConsumerStatus::kAccepted;
        case AidlFrameStatus::DROPPED:
            return FrameConsumerStatus::kDropped;
        case AidlFrameStatus::NO_ACTIVE_STREAM:
            return FrameConsumerStatus::kNoActiveStream;
        case AidlFrameStatus::STALE_GENERATION:
            return FrameConsumerStatus::kStaleGeneration;
        case AidlFrameStatus::BUFFER_UNKNOWN:
            return FrameConsumerStatus::kBufferUnknown;
        case AidlFrameStatus::UNSUPPORTED_BUFFER:
            return FrameConsumerStatus::kUnsupportedBuffer;
        case AidlFrameStatus::INTERNAL_ERROR:
            return FrameConsumerStatus::kInternalError;
    }
    return FrameConsumerStatus::kInternalError;
}

std::shared_ptr<AidlFrameConsumer> ConnectServiceManager(const std::string& serviceName) {
    ndk::SpAIBinder binder(AServiceManager_checkService(serviceName.c_str()));
    return binder.get() == nullptr ? nullptr : AidlFrameConsumer::fromBinder(binder);
}

class AidlFrameConsumerEndpoint final : public FrameConsumerEndpoint {
  public:
    explicit AidlFrameConsumerEndpoint(AidlFrameConsumerEndpointConfig config)
        : config_(std::move(config)) {
        if (!config_.connector) {
            config_.connector = ConnectServiceManager;
        }
        if (config_.state_refresh_interval <= std::chrono::milliseconds::zero()) {
            config_.state_refresh_interval = std::chrono::milliseconds(250);
        }
        for (DisplayId displayId : config_.display_ids) {
            states_.emplace(displayId, FrameConsumerStreamState{});
        }
        worker_ = std::thread([this]() { Run(); });
    }

    ~AidlFrameConsumerEndpoint() override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    FrameConsumerStreamState GetStreamState(DisplayId displayId) override {
        std::lock_guard lock(mutex_);
        const auto found = states_.find(displayId);
        return found == states_.end() ? FrameConsumerStreamState{} : found->second;
    }

    FrameConsumerStatus RegisterBuffer(const StreamBufferRegistration& registration) override {
        const std::shared_ptr<AidlFrameConsumer> consumer = GetConsumer();
        if (consumer == nullptr) {
            return FrameConsumerStatus::kNoActiveStream;
        }
        if (!FitsInt64(registration.display_id) || !FitsInt32(registration.generation) ||
            !FitsInt64(registration.buffer_id) || registration.buffer == nullptr ||
            registration.descriptor.width >
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            registration.descriptor.height >
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            registration.descriptor.layers >
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            registration.descriptor.stride >
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            return FrameConsumerStatus::kUnsupportedBuffer;
        }

        aidl::floral::stream::display::BufferRegistration request;
        request.displayId = static_cast<int64_t>(registration.display_id);
        request.generation = static_cast<int32_t>(registration.generation);
        request.bufferId = static_cast<int64_t>(registration.buffer_id);
        request.buffer.description.width = static_cast<int32_t>(registration.descriptor.width);
        request.buffer.description.height = static_cast<int32_t>(registration.descriptor.height);
        request.buffer.description.layers = static_cast<int32_t>(registration.descriptor.layers);
        request.buffer.description.format =
                static_cast<AidlPixelFormat>(registration.descriptor.format);
        request.buffer.description.usage =
                static_cast<AidlBufferUsage>(registration.descriptor.usage);
        request.buffer.description.stride = static_cast<int32_t>(registration.descriptor.stride);
        request.buffer.handle = android::dupToAidl(registration.buffer);

        AidlFrameStatus status = AidlFrameStatus::INTERNAL_ERROR;
        if (!consumer->registerBuffer(request, &status).isOk()) {
            Disconnect(consumer);
            return FrameConsumerStatus::kNoActiveStream;
        }
        return FromAidlStatus(status);
    }

    StreamFrameResult SubmitFrame(StreamFrameRequest request) override {
        StreamFrameResult result;
        const std::shared_ptr<AidlFrameConsumer> consumer = GetConsumer();
        if (consumer == nullptr) {
            result.status = FrameConsumerStatus::kNoActiveStream;
            return result;
        }
        if (!FitsInt64(request.display_id) || !FitsInt32(request.generation) ||
            !FitsInt64(request.buffer_id) || !FitsInt64(request.source_sequence)) {
            result.status = FrameConsumerStatus::kInternalError;
            return result;
        }

        aidl::floral::stream::display::FrameRequest aidlRequest;
        aidlRequest.displayId = static_cast<int64_t>(request.display_id);
        aidlRequest.generation = static_cast<int32_t>(request.generation);
        aidlRequest.bufferId = static_cast<int64_t>(request.buffer_id);
        aidlRequest.sourceSequence = static_cast<int64_t>(request.source_sequence);
        aidlRequest.frameSubmitTimeNs = request.frame_submit_time_nanos;
        aidlRequest.presentationTimeNs = request.presentation_time_nanos;
        aidlRequest.dataspace = request.dataspace;
        aidlRequest.acquireFence = ndk::ScopedFileDescriptor(request.acquire_fence.release());

        aidl::floral::stream::display::FrameResult aidlResult;
        if (!consumer->submitFrame(aidlRequest, &aidlResult).isOk()) {
            Disconnect(consumer);
            result.status = FrameConsumerStatus::kNoActiveStream;
            return result;
        }
        result.status = FromAidlStatus(aidlResult.status);
        result.release_fence = DuplicateFence(aidlResult.releaseFence);
        if (aidlResult.releaseFence.get() >= 0 && !result.release_fence.ok()) {
            result.status = FrameConsumerStatus::kInternalError;
        }
        return result;
    }

  private:
    std::shared_ptr<AidlFrameConsumer> GetConsumer() const {
        std::lock_guard lock(mutex_);
        return consumer_;
    }

    void Disconnect(const std::shared_ptr<AidlFrameConsumer>& failedConsumer) {
        std::lock_guard lock(mutex_);
        if (consumer_ != failedConsumer) {
            return;
        }
        consumer_.reset();
        for (auto& [displayId, state] : states_) {
            (void)displayId;
            state.accepting_frames = false;
        }
        wake_.notify_all();
    }

    void Run() {
        while (true) {
            {
                std::lock_guard lock(mutex_);
                if (stopping_) {
                    return;
                }
            }

            std::shared_ptr<AidlFrameConsumer> consumer = GetConsumer();
            if (consumer == nullptr) {
                consumer = config_.connector(config_.service_name);
                if (consumer != nullptr) {
                    std::lock_guard lock(mutex_);
                    if (!stopping_) {
                        consumer_ = consumer;
                    }
                }
            }

            if (consumer != nullptr) {
                for (DisplayId displayId : config_.display_ids) {
                    aidl::floral::stream::display::StreamState state;
                    if (!FitsInt64(displayId) ||
                        !consumer->getStreamState(static_cast<int64_t>(displayId), &state).isOk()) {
                        Disconnect(consumer);
                        break;
                    }
                    FrameConsumerStreamState cached;
                    if (state.displayId == static_cast<int64_t>(displayId) &&
                        state.generation >= 0) {
                        cached.generation = static_cast<uint32_t>(state.generation);
                        cached.accepting_frames = state.acceptingFrames;
                    }
                    std::lock_guard lock(mutex_);
                    states_[displayId] = cached;
                }
            }

            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, config_.state_refresh_interval, [this]() { return stopping_; });
            if (stopping_) {
                return;
            }
        }
    }

    // Service connection and cached stream state.
    AidlFrameConsumerEndpointConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::shared_ptr<AidlFrameConsumer> consumer_;
    std::unordered_map<DisplayId, FrameConsumerStreamState> states_;
    bool stopping_ = false;

    // State refresh never runs on SurfaceFlinger's present path.
    std::thread worker_;
};

}  // namespace

std::shared_ptr<FrameConsumerEndpoint> CreateAidlFrameConsumerEndpoint(
        AidlFrameConsumerEndpointConfig config) {
    if (config.service_name.empty() || config.display_ids.empty()) {
        return nullptr;
    }
    return std::make_shared<AidlFrameConsumerEndpoint>(std::move(config));
}

}  // namespace floral::display
