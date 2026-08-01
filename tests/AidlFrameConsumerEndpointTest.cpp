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

#include <aidl/floral/device/display/BnFrameConsumer.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace floral::display {
namespace {

using AidlFrameStatus = aidl::floral::device::display::FrameStatus;

class FakeAidlFrameConsumer final : public aidl::floral::device::display::BnFrameConsumer {
  public:
    ndk::ScopedAStatus getStreamState(int64_t displayId,
                                      aidl::floral::device::display::StreamState* result) override {
        std::lock_guard lock(mutex_);
        result->displayId = displayId;
        result->generation = generation_;
        result->acceptingFrames = accepting_frames_;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus registerBuffer(
            const aidl::floral::device::display::BufferRegistration& registration,
            AidlFrameStatus* result) override {
        std::lock_guard lock(mutex_);
        ++register_count_;
        last_buffer_id_ = registration.bufferId;
        received_valid_handle_ = !registration.buffer.handle.fds.empty() &&
                                 registration.buffer.handle.fds.front().get() >= 0;
        *result = AidlFrameStatus::ACCEPTED;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus submitFrame(const aidl::floral::device::display::FrameRequest& request,
                                   aidl::floral::device::display::FrameResult* result) override {
        std::lock_guard lock(mutex_);
        ++submit_count_;
        last_source_sequence_ = request.sourceSequence;
        result->status = AidlFrameStatus::ACCEPTED;
        if (request.acquireFence.get() >= 0) {
            result->releaseFence = ndk::ScopedFileDescriptor(
                    fcntl(request.acquireFence.get(), F_DUPFD_CLOEXEC, 0));
        }
        return ndk::ScopedAStatus::ok();
    }

    void SetStreamState(int32_t generation, bool acceptingFrames) {
        std::lock_guard lock(mutex_);
        generation_ = generation;
        accepting_frames_ = acceptingFrames;
    }

    uint64_t register_count() const {
        std::lock_guard lock(mutex_);
        return register_count_;
    }

    uint64_t submit_count() const {
        std::lock_guard lock(mutex_);
        return submit_count_;
    }

    int64_t last_buffer_id() const {
        std::lock_guard lock(mutex_);
        return last_buffer_id_;
    }

    int64_t last_source_sequence() const {
        std::lock_guard lock(mutex_);
        return last_source_sequence_;
    }

    bool received_valid_handle() const {
        std::lock_guard lock(mutex_);
        return received_valid_handle_;
    }

  private:
    mutable std::mutex mutex_;
    int32_t generation_ = 0;
    bool accepting_frames_ = false;
    uint64_t register_count_ = 0;
    uint64_t submit_count_ = 0;
    int64_t last_buffer_id_ = -1;
    int64_t last_source_sequence_ = -1;
    bool received_valid_handle_ = false;
};

bool WaitForActiveState(const std::shared_ptr<FrameConsumerEndpoint>& endpoint,
                        uint32_t generation) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const FrameConsumerStreamState state = endpoint->GetStreamState(0);
        if (state.accepting_frames && state.generation == generation) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

TEST(AidlFrameConsumerEndpointTest, CachesStateAndTranslatesFrames) {
    auto service = ndk::SharedRefBase::make<FakeAidlFrameConsumer>();
    service->SetStreamState(9, true);
    AidlFrameConsumerEndpointConfig config;
    config.display_ids = {0};
    config.state_refresh_interval = std::chrono::milliseconds(2);
    config.connector = [service](const std::string&) { return service; };
    std::shared_ptr<FrameConsumerEndpoint> endpoint =
            CreateAidlFrameConsumerEndpoint(std::move(config));
    ASSERT_NE(endpoint, nullptr);
    ASSERT_TRUE(WaitForActiveState(endpoint, 9));

    int handlePipe[2];
    ASSERT_EQ(pipe(handlePipe), 0);
    native_handle_t* handle = native_handle_create(1, 0);
    ASSERT_NE(handle, nullptr);
    handle->data[0] = handlePipe[0];

    StreamBufferRegistration registration;
    registration.display_id = 0;
    registration.generation = 9;
    registration.buffer_id = 77;
    registration.buffer = handle;
    registration.descriptor.width = 1280;
    registration.descriptor.height = 720;
    registration.descriptor.layers = 1;
    registration.descriptor.format = 1;
    registration.descriptor.usage = 2;
    registration.descriptor.stride = 1280;
    EXPECT_EQ(endpoint->RegisterBuffer(registration), FrameConsumerStatus::kAccepted);
    EXPECT_EQ(service->register_count(), 1u);
    EXPECT_EQ(service->last_buffer_id(), 77);
    EXPECT_TRUE(service->received_valid_handle());

    int fencePipe[2];
    ASSERT_EQ(pipe(fencePipe), 0);
    StreamFrameRequest request;
    request.display_id = 0;
    request.generation = 9;
    request.buffer_id = 77;
    request.source_sequence = 123;
    request.acquire_fence.reset(fencePipe[0]);
    StreamFrameResult result = endpoint->SubmitFrame(std::move(request));
    EXPECT_EQ(result.status, FrameConsumerStatus::kAccepted);
    EXPECT_TRUE(result.release_fence.ok());
    EXPECT_EQ(service->submit_count(), 1u);
    EXPECT_EQ(service->last_source_sequence(), 123);

    close(fencePipe[1]);
    native_handle_close(handle);
    native_handle_delete(handle);
    close(handlePipe[1]);
}

TEST(AidlFrameConsumerEndpointTest, StartsInactiveUntilStateIsRefreshed) {
    auto service = ndk::SharedRefBase::make<FakeAidlFrameConsumer>();
    AidlFrameConsumerEndpointConfig config;
    config.display_ids = {0};
    config.state_refresh_interval = std::chrono::milliseconds(2);
    config.connector = [service](const std::string&) { return service; };
    std::shared_ptr<FrameConsumerEndpoint> endpoint =
            CreateAidlFrameConsumerEndpoint(std::move(config));
    ASSERT_NE(endpoint, nullptr);

    const FrameConsumerStreamState state = endpoint->GetStreamState(0);
    EXPECT_FALSE(state.accepting_frames);
}

}  // namespace
}  // namespace floral::display
