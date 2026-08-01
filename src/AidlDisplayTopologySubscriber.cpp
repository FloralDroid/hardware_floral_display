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

#include "floral/display/AidlDisplayTopologySubscriber.h"

#include "floral/display/DisplayRegistry.h"

#include <aidl/floral/display/topology/BnDisplayTopologyListener.h>
#include <aidl/floral/display/topology/PhysicalDisplaySpec.h>
#include <aidl/floral/display/topology/TopologySnapshot.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include <algorithm>
#include <cinttypes>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace floral::display {
namespace {

using AidlDisplaySpec = aidl::floral::display::topology::PhysicalDisplaySpec;
using AidlDisplayTopologyState = aidl::floral::display::topology::IDisplayTopologyState;
using AidlTopologySnapshot = aidl::floral::display::topology::TopologySnapshot;

constexpr int32_t kMinimumDimension = 320;
constexpr int32_t kMaximumDimension = 7680;
constexpr int32_t kMinimumDpi = 72;
constexpr int32_t kMaximumDpi = 640;
constexpr int32_t kMaximumRefreshRateHz = 60;

std::shared_ptr<AidlDisplayTopologyState> ConnectServiceManager(const std::string& serviceName) {
    ndk::SpAIBinder binder(AServiceManager_checkService(serviceName.c_str()));
    return binder.get() == nullptr ? nullptr : AidlDisplayTopologyState::fromBinder(binder);
}

bool ConvertDisplaySpec(const AidlDisplaySpec& source, DisplayConfig* result) {
    if (result == nullptr || source.displayId <= 1 || source.port <= 0 ||
        source.port > static_cast<int32_t>(std::numeric_limits<uint8_t>::max()) ||
        source.width < kMinimumDimension || source.width > kMaximumDimension ||
        source.height < kMinimumDimension || source.height > kMaximumDimension ||
        source.dpi < kMinimumDpi || source.dpi > kMaximumDpi || source.name.empty() ||
        source.supportedRefreshRatesHz.empty() || source.activeRefreshRateHz <= 0 ||
        source.activeRefreshRateHz > kMaximumRefreshRateHz) {
        return false;
    }

    std::vector<uint32_t> refreshRates;
    refreshRates.reserve(source.supportedRefreshRatesHz.size());
    bool containsActiveRate = false;
    for (int32_t refreshRateHz : source.supportedRefreshRatesHz) {
        if (refreshRateHz <= 0 || refreshRateHz > kMaximumRefreshRateHz) {
            return false;
        }
        containsActiveRate |= refreshRateHz == source.activeRefreshRateHz;
        refreshRates.push_back(static_cast<uint32_t>(refreshRateHz));
    }
    if (!containsActiveRate) {
        return false;
    }

    result->id = static_cast<DisplayId>(source.displayId);
    result->port = static_cast<uint8_t>(source.port);
    result->width = static_cast<uint32_t>(source.width);
    result->height = static_cast<uint32_t>(source.height);
    result->dpi = static_cast<uint32_t>(source.dpi);
    result->vsync_period_nanos =
            RefreshRateToVsyncPeriodNanos(static_cast<uint32_t>(source.activeRefreshRateHz));
    result->supported_refresh_rates_hz = std::move(refreshRates);
    result->connection_type = ConnectionType::kExternal;
    result->name = source.name;
    return true;
}

bool ConvertSnapshot(const AidlTopologySnapshot& source, std::vector<DisplayConfig>* displays) {
    if (displays == nullptr || source.generation <= 0) {
        return false;
    }

    std::vector<DisplayConfig> converted;
    converted.reserve(source.externalDisplays.size());
    std::unordered_set<DisplayId> displayIds;
    std::unordered_set<uint8_t> ports;
    for (const AidlDisplaySpec& display : source.externalDisplays) {
        DisplayConfig config;
        if (!ConvertDisplaySpec(display, &config) || !displayIds.insert(config.id).second ||
            !ports.insert(config.port).second) {
            return false;
        }
        converted.push_back(std::move(config));
    }
    std::sort(converted.begin(), converted.end(),
              [](const DisplayConfig& left, const DisplayConfig& right) {
                  return left.id < right.id;
              });
    *displays = std::move(converted);
    return true;
}

class TopologySnapshotApplier {
  public:
    explicit TopologySnapshotApplier(DisplayRegistry* registry) : registry_(registry) {}

    uint64_t BeginServiceEpoch() {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return 0;
        }
        active_epoch_ =
                active_epoch_ == std::numeric_limits<uint64_t>::max() ? 1 : active_epoch_ + 1;
        last_generation_ = 0;
        last_rejected_generation_ = 0;
        return active_epoch_;
    }

    bool Apply(uint64_t epoch, const AidlTopologySnapshot& snapshot) {
        std::vector<DisplayConfig> displays;
        const bool valid = ConvertSnapshot(snapshot, &displays);

        std::lock_guard lock(mutex_);
        if (stopping_ || registry_ == nullptr || epoch != active_epoch_) {
            return false;
        }
        if (!valid) {
            if (last_rejected_generation_ != snapshot.generation) {
                ALOGW("Rejected invalid display topology generation %" PRId64, snapshot.generation);
                last_rejected_generation_ = snapshot.generation;
            }
            return false;
        }

        const uint64_t generation = static_cast<uint64_t>(snapshot.generation);
        if (generation <= last_generation_) {
            return true;
        }
        const DisplayTopologyResult result =
                registry_->ReplaceExternalDisplays(std::move(displays));
        if (result != DisplayTopologyResult::kSuccess) {
            ALOGW("Failed to apply display topology generation %" PRIu64 ": result=%u", generation,
                  static_cast<unsigned int>(result));
            return false;
        }
        last_generation_ = generation;
        last_rejected_generation_ = 0;
        return true;
    }

    void EndServiceEpoch(uint64_t epoch) {
        std::lock_guard lock(mutex_);
        if (epoch != active_epoch_) {
            return;
        }
        active_epoch_ =
                active_epoch_ == std::numeric_limits<uint64_t>::max() ? 1 : active_epoch_ + 1;
        last_generation_ = 0;
        last_rejected_generation_ = 0;
    }

    bool ClearExternalDisplays() {
        std::lock_guard lock(mutex_);
        if (stopping_ || registry_ == nullptr) {
            return false;
        }
        return registry_->ReplaceExternalDisplays({}) == DisplayTopologyResult::kSuccess;
    }

    void Stop() {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        registry_ = nullptr;
    }

  private:
    // Serializes binder callbacks, health snapshots, and registry mutations.
    std::mutex mutex_;
    DisplayRegistry* registry_ = nullptr;
    uint64_t active_epoch_ = 0;
    uint64_t last_generation_ = 0;
    int64_t last_rejected_generation_ = 0;
    bool stopping_ = false;
};

class TopologyListener final : public aidl::floral::display::topology::BnDisplayTopologyListener {
  public:
    TopologyListener(std::shared_ptr<TopologySnapshotApplier> applier, uint64_t epoch)
        : applier_(std::move(applier)), epoch_(epoch) {}

    ndk::ScopedAStatus onTopologyChanged(const AidlTopologySnapshot& snapshot) override {
        (void)applier_->Apply(epoch_, snapshot);
        return ndk::ScopedAStatus::ok();
    }

  private:
    const std::shared_ptr<TopologySnapshotApplier> applier_;
    const uint64_t epoch_;
};

class AidlDisplayTopologySubscription final : public DisplayTopologySubscription {
  public:
    AidlDisplayTopologySubscription(AidlDisplayTopologySubscriberConfig config,
                                    DisplayRegistry* registry)
        : config_(std::move(config)),
          applier_(std::make_shared<TopologySnapshotApplier>(registry)) {
        if (!config_.connector) {
            config_.connector = ConnectServiceManager;
        }
        if (config_.reconnect_interval <= std::chrono::milliseconds::zero()) {
            config_.reconnect_interval = std::chrono::milliseconds(250);
        }
        if (config_.health_check_interval <= std::chrono::milliseconds::zero()) {
            config_.health_check_interval = std::chrono::milliseconds(1000);
        }
        if (config_.service_loss_lease <= std::chrono::milliseconds::zero()) {
            config_.service_loss_lease = std::chrono::milliseconds(3000);
        }
        if (config_.start_binder_thread_pool) {
            ABinderProcess_startThreadPool();
        }
        worker_ = std::thread([this]() { Run(); });
    }

    ~AidlDisplayTopologySubscription() override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        // Any callback already in flight finishes before the registry can be
        // destroyed. Later callbacks retain the listener but become no-ops.
        applier_->Stop();
    }

  private:
    bool WaitFor(std::chrono::milliseconds interval) {
        std::unique_lock lock(mutex_);
        return wake_.wait_for(lock, interval, [this]() { return stopping_; });
    }

    bool IsStopping() const {
        std::lock_guard lock(mutex_);
        return stopping_;
    }

    std::chrono::milliseconds BoundedWait(std::chrono::milliseconds maximum) const {
        if (!service_loss_deadline_.has_value()) {
            return maximum;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= *service_loss_deadline_) {
            return std::chrono::milliseconds::zero();
        }
        return std::min(maximum, std::chrono::duration_cast<std::chrono::milliseconds>(
                                         *service_loss_deadline_ - now));
    }

    void BeginServiceLossLease() {
        if (!has_authoritative_snapshot_ || service_loss_deadline_.has_value()) {
            return;
        }
        service_loss_deadline_ = std::chrono::steady_clock::now() + config_.service_loss_lease;
        ALOGI("Display topology service lost; retaining external displays for %lld ms",
              static_cast<long long>(config_.service_loss_lease.count()));
    }

    void ExpireServiceLossLeaseIfNeeded() {
        if (!service_loss_deadline_.has_value() ||
            std::chrono::steady_clock::now() < *service_loss_deadline_) {
            return;
        }
        service_loss_deadline_.reset();
        has_authoritative_snapshot_ = false;
        if (applier_->ClearExternalDisplays()) {
            ALOGI("Display topology service loss lease expired; external displays were cleared");
        } else {
            ALOGW("Failed to clear external displays after display topology service loss");
        }
    }

    void Run() {
        while (!IsStopping()) {
            ExpireServiceLossLeaseIfNeeded();
            const std::shared_ptr<AidlDisplayTopologyState> service =
                    config_.connector(config_.service_name);
            if (service == nullptr) {
                if (WaitFor(BoundedWait(config_.reconnect_interval))) {
                    return;
                }
                continue;
            }

            const uint64_t epoch = applier_->BeginServiceEpoch();
            const std::shared_ptr<TopologyListener> listener =
                    ndk::SharedRefBase::make<TopologyListener>(applier_, epoch);
            if (epoch == 0 || !service->registerListener(listener).isOk()) {
                applier_->EndServiceEpoch(epoch);
                BeginServiceLossLease();
                if (WaitFor(BoundedWait(config_.reconnect_interval))) {
                    return;
                }
                continue;
            }
            ALOGI("Connected display topology service: %s", config_.service_name.c_str());

            while (!IsStopping()) {
                AidlTopologySnapshot snapshot;
                if (!service->getSnapshot(&snapshot).isOk()) {
                    ALOGW("Display topology service disconnected");
                    break;
                }
                if (applier_->Apply(epoch, snapshot)) {
                    has_authoritative_snapshot_ = true;
                    service_loss_deadline_.reset();
                } else {
                    BeginServiceLossLease();
                }
                ExpireServiceLossLeaseIfNeeded();
                if (WaitFor(BoundedWait(config_.health_check_interval))) {
                    break;
                }
            }

            (void)service->unregisterListener(listener);
            applier_->EndServiceEpoch(epoch);
            if (!IsStopping()) {
                BeginServiceLossLease();
            }
            if (!IsStopping() && WaitFor(BoundedWait(config_.reconnect_interval))) {
                return;
            }
        }
    }

    // Service discovery and snapshot application state.
    AidlDisplayTopologySubscriberConfig config_;
    const std::shared_ptr<TopologySnapshotApplier> applier_;

    // Reconnect and health-check worker lifecycle.
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::thread worker_;

    // Only the reconnect worker mutates service-authority state.
    bool has_authoritative_snapshot_ = false;
    std::optional<std::chrono::steady_clock::time_point> service_loss_deadline_;
};

}  // namespace

std::unique_ptr<DisplayTopologySubscription> CreateAidlDisplayTopologySubscription(
        AidlDisplayTopologySubscriberConfig config, DisplayRegistry* registry) {
    if (registry == nullptr || config.service_name.empty()) {
        return nullptr;
    }
    return std::make_unique<AidlDisplayTopologySubscription>(std::move(config), registry);
}

}  // namespace floral::display
