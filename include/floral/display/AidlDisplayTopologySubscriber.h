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

#include <aidl/floral/device/display/topology/IDisplayTopologyState.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace floral::display {

class DisplayRegistry;

inline constexpr char kDefaultDisplayTopologyStateService[] =
        "floral.device.display.topology.IDisplayTopologyState/default";

struct AidlDisplayTopologySubscriberConfig {
    std::string service_name = kDefaultDisplayTopologyStateService;
    std::chrono::milliseconds reconnect_interval{250};
    std::chrono::milliseconds health_check_interval{1000};
    std::chrono::milliseconds service_loss_lease{3000};
    bool start_binder_thread_pool = true;

    // Tests may inject an in-process service. Production resolves the stable
    // VINTF AIDL instance through the service manager.
    std::function<std::shared_ptr<aidl::floral::display::topology::IDisplayTopologyState>(
            const std::string& serviceName)>
            connector;
};

class DisplayTopologySubscription {
  public:
    virtual ~DisplayTopologySubscription() = default;
};

std::unique_ptr<DisplayTopologySubscription> CreateAidlDisplayTopologySubscription(
        AidlDisplayTopologySubscriberConfig config, DisplayRegistry* registry);

}  // namespace floral::display
