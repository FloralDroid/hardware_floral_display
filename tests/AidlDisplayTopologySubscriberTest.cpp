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

#include "floral/display/AidlDisplayTopologySubscriber.h"

#include "floral/display/DisplayRegistry.h"

#include <aidl/floral/device/display/topology/BnDisplayTopologyState.h>
#include <aidl/floral/device/display/topology/PhysicalDisplaySpec.h>
#include <aidl/floral/device/display/topology/TopologySnapshot.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace floral::display {
namespace {

using AidlDisplaySpec = aidl::floral::display::topology::PhysicalDisplaySpec;
using AidlDisplayTopologyListener = aidl::floral::display::topology::IDisplayTopologyListener;
using AidlTopologySnapshot = aidl::floral::display::topology::TopologySnapshot;

class FakeDisplayTopologyState final
    : public aidl::floral::display::topology::BnDisplayTopologyState {
  public:
    explicit FakeDisplayTopologyState(AidlTopologySnapshot snapshot)
        : snapshot_(std::move(snapshot)) {}

    ndk::ScopedAStatus getSnapshot(AidlTopologySnapshot* result) override {
        std::lock_guard lock(mutex_);
        if (fail_snapshot_) {
            return ndk::ScopedAStatus::fromServiceSpecificError(1);
        }
        *result = snapshot_;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus registerListener(
            const std::shared_ptr<AidlDisplayTopologyListener>& listener) override {
        AidlTopologySnapshot snapshot;
        {
            std::lock_guard lock(mutex_);
            listener_ = listener;
            snapshot = snapshot_;
        }
        if (listener != nullptr) {
            (void)listener->onTopologyChanged(snapshot);
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus unregisterListener(
            const std::shared_ptr<AidlDisplayTopologyListener>& listener) override {
        std::lock_guard lock(mutex_);
        if (listener_ != nullptr && listener != nullptr &&
            listener_->asBinder().get() == listener->asBinder().get()) {
            retired_listeners_.push_back(listener_);
            listener_.reset();
            ++unregister_count_;
        }
        return ndk::ScopedAStatus::ok();
    }

    void Publish(AidlTopologySnapshot snapshot) {
        std::shared_ptr<AidlDisplayTopologyListener> listener;
        {
            std::lock_guard lock(mutex_);
            snapshot_ = snapshot;
            listener = listener_;
        }
        if (listener != nullptr) {
            (void)listener->onTopologyChanged(snapshot);
        }
    }

    void SetSnapshotFailure(bool fail) {
        std::lock_guard lock(mutex_);
        fail_snapshot_ = fail;
    }

    void PublishToRetiredListener(AidlTopologySnapshot snapshot) {
        std::shared_ptr<AidlDisplayTopologyListener> listener;
        {
            std::lock_guard lock(mutex_);
            if (!retired_listeners_.empty()) {
                listener = retired_listeners_.back();
            }
        }
        if (listener != nullptr) {
            (void)listener->onTopologyChanged(snapshot);
        }
    }

    uint32_t unregister_count() const {
        std::lock_guard lock(mutex_);
        return unregister_count_;
    }

  private:
    mutable std::mutex mutex_;
    AidlTopologySnapshot snapshot_;
    std::shared_ptr<AidlDisplayTopologyListener> listener_;
    std::vector<std::shared_ptr<AidlDisplayTopologyListener>> retired_listeners_;
    bool fail_snapshot_ = false;
    uint32_t unregister_count_ = 0;
};

AidlDisplaySpec ExternalDisplay(int64_t id, int32_t port, int32_t width = 1280) {
    AidlDisplaySpec display;
    display.displayId = id;
    display.port = port;
    display.width = width;
    display.height = 720;
    display.dpi = 320;
    display.supportedRefreshRatesHz = {15, 30, 60};
    display.activeRefreshRateHz = 60;
    display.name = "Floral External Display " + std::to_string(id);
    return display;
}

AidlTopologySnapshot Snapshot(int64_t generation,
                              std::vector<AidlDisplaySpec> externalDisplays = {}) {
    AidlTopologySnapshot snapshot;
    snapshot.generation = generation;
    snapshot.externalDisplays = std::move(externalDisplays);
    return snapshot;
}

bool WaitUntil(const std::function<bool()>& predicate) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

AidlDisplayTopologySubscriberConfig TestConfig(
        const std::shared_ptr<FakeDisplayTopologyState>& service) {
    AidlDisplayTopologySubscriberConfig config;
    config.reconnect_interval = std::chrono::milliseconds(2);
    config.health_check_interval = std::chrono::milliseconds(5);
    config.start_binder_thread_pool = false;
    config.connector = [service](const std::string&) { return service; };
    return config;
}

TEST(AidlDisplayTopologySubscriberTest, AppliesGenerationOrderedExternalSnapshots) {
    auto service = ndk::SharedRefBase::make<FakeDisplayTopologyState>(Snapshot(1));
    std::mutex eventsMutex;
    std::vector<std::pair<hwc2_display_t, bool>> events;
    DisplayRegistry registry(DisplayConfig{}, {});
    registry.SetHotplugCallback([&eventsMutex, &events](hwc2_display_t display, bool connected) {
        std::lock_guard lock(eventsMutex);
        events.emplace_back(display, connected);
    });

    std::unique_ptr<DisplayTopologySubscription> subscription =
            CreateAidlDisplayTopologySubscription(TestConfig(service), &registry);
    ASSERT_NE(subscription, nullptr);

    service->Publish(Snapshot(2, {ExternalDisplay(2, 1)}));
    ASSERT_TRUE(WaitUntil([&registry]() { return registry.Get(2) != nullptr; }));
    const std::shared_ptr<Display> originalDisplay = registry.Get(2);
    ASSERT_NE(originalDisplay, nullptr);

    // Stale and invalid generations cannot mutate the applied topology.
    service->Publish(Snapshot(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(registry.Get(2), originalDisplay);
    service->Publish(Snapshot(3, {ExternalDisplay(1, 2)}));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(registry.Get(2), originalDisplay);

    // A valid snapshot with the rejected generation is still accepted.
    service->Publish(Snapshot(3, {ExternalDisplay(2, 1, 1920)}));
    ASSERT_TRUE(WaitUntil([&registry, &originalDisplay]() {
        const std::shared_ptr<Display> display = registry.Get(2);
        return display != nullptr && display != originalDisplay;
    }));
    service->Publish(Snapshot(4));
    ASSERT_TRUE(WaitUntil([&registry]() { return registry.Get(2) == nullptr; }));

    subscription.reset();
    EXPECT_EQ(service->unregister_count(), 1u);
    std::lock_guard lock(eventsMutex);
    EXPECT_EQ(events, (std::vector<std::pair<hwc2_display_t, bool>>{
                              {DisplayRegistry::kPrimaryDisplayId, true},
                              {2, true},
                              {2, false},
                              {2, true},
                              {2, false}}));
}

TEST(AidlDisplayTopologySubscriberTest, AcceptsGenerationResetAfterServiceReconnect) {
    auto first = ndk::SharedRefBase::make<FakeDisplayTopologyState>(
            Snapshot(9, {ExternalDisplay(2, 1)}));
    auto second = ndk::SharedRefBase::make<FakeDisplayTopologyState>(Snapshot(1));
    std::atomic<uint32_t> connectionCount{0};

    DisplayRegistry registry(DisplayConfig{}, {});
    AidlDisplayTopologySubscriberConfig config;
    config.reconnect_interval = std::chrono::milliseconds(2);
    config.health_check_interval = std::chrono::milliseconds(5);
    config.start_binder_thread_pool = false;
    config.connector = [first, second, &connectionCount](const std::string&) {
        return connectionCount.fetch_add(1) == 0 ? first : second;
    };
    std::unique_ptr<DisplayTopologySubscription> subscription =
            CreateAidlDisplayTopologySubscription(std::move(config), &registry);
    ASSERT_NE(subscription, nullptr);
    ASSERT_TRUE(WaitUntil([&registry]() { return registry.Get(2) != nullptr; }));

    first->SetSnapshotFailure(true);
    ASSERT_TRUE(WaitUntil([&registry]() { return registry.Get(2) == nullptr; }));
    EXPECT_GE(connectionCount.load(), 2u);

    // A callback already queued by the old service epoch cannot override the
    // new service's reset generation.
    first->PublishToRetiredListener(Snapshot(10, {ExternalDisplay(3, 2)}));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(registry.Get(3), nullptr);

    subscription.reset();
    EXPECT_EQ(second->unregister_count(), 1u);
}

TEST(AidlDisplayTopologySubscriberTest, ClearsDisplaysAfterServiceLossLease) {
    auto service = ndk::SharedRefBase::make<FakeDisplayTopologyState>(
            Snapshot(4, {ExternalDisplay(2, 1)}));
    std::atomic<uint32_t> connectionCount{0};

    DisplayRegistry registry(DisplayConfig{}, {});
    AidlDisplayTopologySubscriberConfig config = TestConfig(service);
    config.service_loss_lease = std::chrono::milliseconds(20);
    config.connector = [service, &connectionCount](const std::string&) {
        return connectionCount.fetch_add(1) == 0 ? service : nullptr;
    };
    std::unique_ptr<DisplayTopologySubscription> subscription =
            CreateAidlDisplayTopologySubscription(std::move(config), &registry);
    ASSERT_NE(subscription, nullptr);
    ASSERT_TRUE(WaitUntil([&registry]() { return registry.Get(2) != nullptr; }));

    service->SetSnapshotFailure(true);
    ASSERT_TRUE(WaitUntil([&registry]() { return registry.Get(2) == nullptr; }));
    EXPECT_GE(connectionCount.load(), 2u);
}

TEST(AidlDisplayTopologySubscriberTest, CancelsServiceLossLeaseAfterValidReconnect) {
    auto first = ndk::SharedRefBase::make<FakeDisplayTopologyState>(
            Snapshot(4, {ExternalDisplay(2, 1)}));
    auto second = ndk::SharedRefBase::make<FakeDisplayTopologyState>(
            Snapshot(1, {ExternalDisplay(2, 1)}));
    std::atomic<uint32_t> connectionCount{0};

    DisplayRegistry registry(DisplayConfig{}, {});
    AidlDisplayTopologySubscriberConfig config = TestConfig(first);
    config.service_loss_lease = std::chrono::milliseconds(20);
    config.connector = [first, second, &connectionCount](const std::string&) {
        return connectionCount.fetch_add(1) == 0 ? first : second;
    };
    std::unique_ptr<DisplayTopologySubscription> subscription =
            CreateAidlDisplayTopologySubscription(std::move(config), &registry);
    ASSERT_NE(subscription, nullptr);
    ASSERT_TRUE(WaitUntil([&registry]() { return registry.Get(2) != nullptr; }));
    const std::shared_ptr<Display> originalDisplay = registry.Get(2);

    first->SetSnapshotFailure(true);
    ASSERT_TRUE(WaitUntil([&connectionCount]() { return connectionCount.load() >= 2; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(registry.Get(2), originalDisplay);
}

}  // namespace
}  // namespace floral::display
