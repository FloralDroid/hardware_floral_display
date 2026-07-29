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

#include <aidl/floral/stream/display/IFrameConsumer.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "floral/display/StreamFrameSink.h"

namespace floral::display {

inline constexpr char kDefaultFrameConsumerService[] =
        "floral.stream.display.IFrameConsumer/default";

struct AidlFrameConsumerEndpointConfig {
    std::string service_name = kDefaultFrameConsumerService;
    std::vector<DisplayId> display_ids;
    std::chrono::milliseconds state_refresh_interval{250};

    // Tests may provide a deterministic connector. Production uses the
    // service manager when this callback is empty.
    std::function<std::shared_ptr<aidl::floral::stream::display::IFrameConsumer>(
            const std::string& serviceName)>
            connector;
};

std::shared_ptr<FrameConsumerEndpoint> CreateAidlFrameConsumerEndpoint(
        AidlFrameConsumerEndpointConfig config);

}  // namespace floral::display
