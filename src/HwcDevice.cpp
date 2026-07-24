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

#include "floral/display/HwcDevice.h"

#include <android-base/properties.h>
#include <log/log.h>

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace floral::display {
namespace {

template <typename Function, typename Candidate>
hwc2_function_pointer_t AsFunction(Candidate candidate) {
    static_assert(std::is_same_v<Function, Candidate>, "HWC2 function signature mismatch");
    return reinterpret_cast<hwc2_function_pointer_t>(candidate);
}

template <typename Method, typename... Args>
int32_t WithDisplay(hwc2_device_t* device, hwc2_display_t displayId, Method method, Args... args) {
    Display* display = HwcDevice::From(device)->GetDisplay(displayId);
    if (display == nullptr) {
        return HWC2_ERROR_BAD_DISPLAY;
    }
    return (display->*method)(args...);
}

int32_t CreateVirtualDisplay(hwc2_device_t* device, uint32_t width, uint32_t height,
                             int32_t* format, hwc2_display_t* outDisplay) {
    (void)device;
    (void)width;
    (void)height;
    (void)format;
    (void)outDisplay;
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t DestroyVirtualDisplay(hwc2_device_t* device, hwc2_display_t display) {
    (void)device;
    (void)display;
    return HWC2_ERROR_BAD_DISPLAY;
}

void Dump(hwc2_device_t* device, uint32_t* outSize, char* outBuffer) {
    if (outSize == nullptr) {
        return;
    }
    const std::string dump = HwcDevice::From(device)->Dump();
    if (outBuffer == nullptr) {
        *outSize = static_cast<uint32_t>(dump.size());
        return;
    }
    const uint32_t count = std::min<uint32_t>(*outSize, static_cast<uint32_t>(dump.size()));
    std::memcpy(outBuffer, dump.data(), count);
    *outSize = count;
}

uint32_t GetMaxVirtualDisplayCount(hwc2_device_t* device) {
    (void)device;
    return 0;
}

int32_t RegisterCallback(hwc2_device_t* device, int32_t descriptor,
                         hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer) {
    return HwcDevice::From(device)->RegisterCallback(descriptor, callbackData, pointer);
}

int32_t AcceptDisplayChanges(hwc2_device_t* device, hwc2_display_t display) {
    return WithDisplay(device, display, &Display::AcceptDisplayChanges);
}

int32_t CreateLayer(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t* outLayer) {
    return WithDisplay(device, display, &Display::CreateLayer, outLayer);
}

int32_t DestroyLayer(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer) {
    return WithDisplay(device, display, &Display::DestroyLayer, layer);
}

int32_t GetActiveConfig(hwc2_device_t* device, hwc2_display_t display, hwc2_config_t* outConfig) {
    return WithDisplay(device, display, &Display::GetActiveConfig, outConfig);
}

int32_t GetChangedCompositionTypes(hwc2_device_t* device, hwc2_display_t display,
                                   uint32_t* outNumElements, hwc2_layer_t* outLayers,
                                   int32_t* outTypes) {
    return WithDisplay(device, display, &Display::GetChangedCompositionTypes, outNumElements,
                       outLayers, outTypes);
}

int32_t GetColorModes(hwc2_device_t* device, hwc2_display_t display, uint32_t* outNumModes,
                      int32_t* outModes) {
    return WithDisplay(device, display, &Display::GetColorModes, outNumModes, outModes);
}

int32_t GetDisplayAttribute(hwc2_device_t* device, hwc2_display_t display, hwc2_config_t config,
                            int32_t attribute, int32_t* outValue) {
    return WithDisplay(device, display, &Display::GetDisplayAttribute, config, attribute, outValue);
}

int32_t GetDisplayConfigs(hwc2_device_t* device, hwc2_display_t display, uint32_t* outNumConfigs,
                          hwc2_config_t* outConfigs) {
    return WithDisplay(device, display, &Display::GetDisplayConfigs, outNumConfigs, outConfigs);
}

int32_t GetDisplayName(hwc2_device_t* device, hwc2_display_t display, uint32_t* outSize,
                       char* outName) {
    return WithDisplay(device, display, &Display::GetDisplayName, outSize, outName);
}

int32_t GetDisplayRequests(hwc2_device_t* device, hwc2_display_t display,
                           int32_t* outDisplayRequests, uint32_t* outNumElements,
                           hwc2_layer_t* outLayers, int32_t* outLayerRequests) {
    return WithDisplay(device, display, &Display::GetDisplayRequests, outDisplayRequests,
                       outNumElements, outLayers, outLayerRequests);
}

int32_t GetDisplayType(hwc2_device_t* device, hwc2_display_t display, int32_t* outType) {
    return WithDisplay(device, display, &Display::GetDisplayType, outType);
}

int32_t GetDozeSupport(hwc2_device_t* device, hwc2_display_t display, int32_t* outSupport) {
    return WithDisplay(device, display, &Display::GetDozeSupport, outSupport);
}

int32_t GetHdrCapabilities(hwc2_device_t* device, hwc2_display_t display, uint32_t* outNumTypes,
                           int32_t* outTypes, float* outMaxLuminance, float* outMaxAverageLuminance,
                           float* outMinLuminance) {
    return WithDisplay(device, display, &Display::GetHdrCapabilities, outNumTypes, outTypes,
                       outMaxLuminance, outMaxAverageLuminance, outMinLuminance);
}

int32_t GetReleaseFences(hwc2_device_t* device, hwc2_display_t display, uint32_t* outNumElements,
                         hwc2_layer_t* outLayers, int32_t* outFences) {
    return WithDisplay(device, display, &Display::GetReleaseFences, outNumElements, outLayers,
                       outFences);
}

int32_t PresentDisplay(hwc2_device_t* device, hwc2_display_t display, int32_t* outPresentFence) {
    return WithDisplay(device, display, &Display::PresentDisplay, outPresentFence);
}

int32_t SetActiveConfig(hwc2_device_t* device, hwc2_display_t display, hwc2_config_t config) {
    return WithDisplay(device, display, &Display::SetActiveConfig, config);
}

int32_t SetClientTarget(hwc2_device_t* device, hwc2_display_t display, buffer_handle_t target,
                        int32_t acquireFence, int32_t dataspace, hwc_region_t damage) {
    return WithDisplay(device, display, &Display::SetClientTarget, target, acquireFence, dataspace,
                       damage);
}

int32_t SetColorMode(hwc2_device_t* device, hwc2_display_t display, int32_t mode) {
    return WithDisplay(device, display, &Display::SetColorMode, mode);
}

int32_t SetColorTransform(hwc2_device_t* device, hwc2_display_t display, const float* matrix,
                          int32_t hint) {
    return WithDisplay(device, display, &Display::SetColorTransform, matrix, hint);
}

int32_t SetOutputBuffer(hwc2_device_t* device, hwc2_display_t display, buffer_handle_t buffer,
                        int32_t releaseFence) {
    return WithDisplay(device, display, &Display::SetOutputBuffer, buffer, releaseFence);
}

int32_t SetPowerMode(hwc2_device_t* device, hwc2_display_t display, int32_t mode) {
    return WithDisplay(device, display, &Display::SetPowerMode, mode);
}

int32_t SetVsyncEnabled(hwc2_device_t* device, hwc2_display_t display, int32_t enabled) {
    return WithDisplay(device, display, &Display::SetVsyncEnabled, enabled);
}

int32_t ValidateDisplay(hwc2_device_t* device, hwc2_display_t display, uint32_t* outNumTypes,
                        uint32_t* outNumRequests) {
    return WithDisplay(device, display, &Display::ValidateDisplay, outNumTypes, outNumRequests);
}

int32_t GetClientTargetSupport(hwc2_device_t* device, hwc2_display_t display, uint32_t width,
                               uint32_t height, int32_t format, int32_t dataspace) {
    return WithDisplay(device, display, &Display::GetClientTargetSupport, width, height, format,
                       dataspace);
}

int32_t GetDisplayIdentificationData(hwc2_device_t* device, hwc2_display_t display,
                                     uint8_t* outPort, uint32_t* outDataSize, uint8_t* outData) {
    return WithDisplay(device, display, &Display::GetDisplayIdentificationData, outPort,
                       outDataSize, outData);
}

int32_t GetDisplayCapabilities(hwc2_device_t* device, hwc2_display_t display,
                               uint32_t* outNumCapabilities, uint32_t* outCapabilities) {
    return WithDisplay(device, display, &Display::GetDisplayCapabilities, outNumCapabilities,
                       outCapabilities);
}

int32_t GetDisplayBrightnessSupport(hwc2_device_t* device, hwc2_display_t display,
                                    bool* outSupport) {
    return WithDisplay(device, display, &Display::GetDisplayBrightnessSupport, outSupport);
}

int32_t SetDisplayBrightness(hwc2_device_t* device, hwc2_display_t display, float brightness) {
    return WithDisplay(device, display, &Display::SetDisplayBrightness, brightness);
}

int32_t GetDisplayConnectionType(hwc2_device_t* device, hwc2_display_t display, uint32_t* outType) {
    return WithDisplay(device, display, &Display::GetDisplayConnectionType, outType);
}

int32_t GetDisplayVsyncPeriod(hwc2_device_t* device, hwc2_display_t display,
                              hwc2_vsync_period_t* outVsyncPeriod) {
    return WithDisplay(device, display, &Display::GetDisplayVsyncPeriod, outVsyncPeriod);
}

int32_t SetActiveConfigWithConstraints(hwc2_device_t* device, hwc2_display_t display,
                                       hwc2_config_t config,
                                       hwc_vsync_period_change_constraints_t* constraints,
                                       hwc_vsync_period_change_timeline_t* outTimeline) {
    return WithDisplay(device, display, &Display::SetActiveConfigWithConstraints, config,
                       constraints, outTimeline);
}

int32_t SetCursorPosition(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                          int32_t x, int32_t y) {
    return WithDisplay(device, display, &Display::SetCursorPosition, layer, x, y);
}

int32_t SetLayerBuffer(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                       buffer_handle_t buffer, int32_t acquireFence) {
    return WithDisplay(device, display, &Display::SetLayerBuffer, layer, buffer, acquireFence);
}

int32_t SetLayerSurfaceDamage(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                              hwc_region_t damage) {
    return WithDisplay(device, display, &Display::SetLayerSurfaceDamage, layer, damage);
}

int32_t SetLayerBlendMode(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                          int32_t mode) {
    return WithDisplay(device, display, &Display::SetLayerBlendMode, layer, mode);
}

int32_t SetLayerColor(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                      hwc_color_t color) {
    return WithDisplay(device, display, &Display::SetLayerColor, layer, color);
}

int32_t SetLayerCompositionType(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                                int32_t type) {
    return WithDisplay(device, display, &Display::SetLayerCompositionType, layer, type);
}

int32_t SetLayerDataspace(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                          int32_t dataspace) {
    return WithDisplay(device, display, &Display::SetLayerDataspace, layer, dataspace);
}

int32_t SetLayerDisplayFrame(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                             hwc_rect_t frame) {
    return WithDisplay(device, display, &Display::SetLayerDisplayFrame, layer, frame);
}

int32_t SetLayerPlaneAlpha(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                           float alpha) {
    return WithDisplay(device, display, &Display::SetLayerPlaneAlpha, layer, alpha);
}

int32_t SetLayerSidebandStream(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                               const native_handle_t* stream) {
    return WithDisplay(device, display, &Display::SetLayerSidebandStream, layer, stream);
}

int32_t SetLayerSourceCrop(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                           hwc_frect_t crop) {
    return WithDisplay(device, display, &Display::SetLayerSourceCrop, layer, crop);
}

int32_t SetLayerTransform(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                          int32_t transform) {
    return WithDisplay(device, display, &Display::SetLayerTransform, layer, transform);
}

int32_t SetLayerVisibleRegion(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                              hwc_region_t visible) {
    return WithDisplay(device, display, &Display::SetLayerVisibleRegion, layer, visible);
}

int32_t SetLayerZOrder(hwc2_device_t* device, hwc2_display_t display, hwc2_layer_t layer,
                       uint32_t z) {
    return WithDisplay(device, display, &Display::SetLayerZOrder, layer, z);
}

uint32_t BoundedProperty(const char* name, uint32_t defaultValue, uint32_t minimum,
                         uint32_t maximum) {
    const uint32_t value = android::base::GetUintProperty<uint32_t>(name, defaultValue, maximum);
    return value < minimum ? defaultValue : value;
}

}  // namespace

HwcDevice::HwcDevice() {
    common.tag = HARDWARE_DEVICE_TAG;
    common.version = HWC_DEVICE_API_VERSION_2_0;
    getCapabilities = [](hwc2_device_t* device, uint32_t* outCount, int32_t* outCapabilities) {
        HwcDevice::From(device)->GetCapabilities(outCount, outCapabilities);
    };
    getFunction = [](hwc2_device_t* device, int32_t descriptor) {
        return HwcDevice::From(device)->GetFunction(descriptor);
    };

    registry_ = std::make_unique<DisplayRegistry>(
            LoadPrimaryDisplayConfig(),
            [this](hwc2_display_t display, int64_t timestamp, int64_t period) {
                EmitVsync(display, timestamp, period);
            });
}

HwcDevice::~HwcDevice() {
    // Stop VSync threads before callback storage is destroyed.
    registry_.reset();
}

Display* HwcDevice::GetDisplay(hwc2_display_t id) const {
    return registry_ == nullptr ? nullptr : registry_->Get(id);
}

void HwcDevice::GetCapabilities(uint32_t* outCount, int32_t* outCapabilities) {
    if (outCount == nullptr) {
        return;
    }
    (void)outCapabilities;
    // No sideband, protected-content, or skip-validate capabilities.
    *outCount = 0;
}

hwc2_function_pointer_t HwcDevice::GetFunction(int32_t descriptor) {
    switch (descriptor) {
        case HWC2_FUNCTION_CREATE_VIRTUAL_DISPLAY:
            return AsFunction<HWC2_PFN_CREATE_VIRTUAL_DISPLAY>(&CreateVirtualDisplay);
        case HWC2_FUNCTION_DESTROY_VIRTUAL_DISPLAY:
            return AsFunction<HWC2_PFN_DESTROY_VIRTUAL_DISPLAY>(&DestroyVirtualDisplay);
        case HWC2_FUNCTION_DUMP:
            return AsFunction<HWC2_PFN_DUMP>(&floral::display::Dump);
        case HWC2_FUNCTION_GET_MAX_VIRTUAL_DISPLAY_COUNT:
            return AsFunction<HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT>(&GetMaxVirtualDisplayCount);
        case HWC2_FUNCTION_REGISTER_CALLBACK:
            return AsFunction<HWC2_PFN_REGISTER_CALLBACK>(&floral::display::RegisterCallback);
        case HWC2_FUNCTION_ACCEPT_DISPLAY_CHANGES:
            return AsFunction<HWC2_PFN_ACCEPT_DISPLAY_CHANGES>(&AcceptDisplayChanges);
        case HWC2_FUNCTION_CREATE_LAYER:
            return AsFunction<HWC2_PFN_CREATE_LAYER>(&CreateLayer);
        case HWC2_FUNCTION_DESTROY_LAYER:
            return AsFunction<HWC2_PFN_DESTROY_LAYER>(&DestroyLayer);
        case HWC2_FUNCTION_GET_ACTIVE_CONFIG:
            return AsFunction<HWC2_PFN_GET_ACTIVE_CONFIG>(&GetActiveConfig);
        case HWC2_FUNCTION_GET_CHANGED_COMPOSITION_TYPES:
            return AsFunction<HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES>(&GetChangedCompositionTypes);
        case HWC2_FUNCTION_GET_COLOR_MODES:
            return AsFunction<HWC2_PFN_GET_COLOR_MODES>(&GetColorModes);
        case HWC2_FUNCTION_GET_DISPLAY_ATTRIBUTE:
            return AsFunction<HWC2_PFN_GET_DISPLAY_ATTRIBUTE>(&GetDisplayAttribute);
        case HWC2_FUNCTION_GET_DISPLAY_CONFIGS:
            return AsFunction<HWC2_PFN_GET_DISPLAY_CONFIGS>(&GetDisplayConfigs);
        case HWC2_FUNCTION_GET_DISPLAY_NAME:
            return AsFunction<HWC2_PFN_GET_DISPLAY_NAME>(&GetDisplayName);
        case HWC2_FUNCTION_GET_DISPLAY_REQUESTS:
            return AsFunction<HWC2_PFN_GET_DISPLAY_REQUESTS>(&GetDisplayRequests);
        case HWC2_FUNCTION_GET_DISPLAY_TYPE:
            return AsFunction<HWC2_PFN_GET_DISPLAY_TYPE>(&GetDisplayType);
        case HWC2_FUNCTION_GET_DOZE_SUPPORT:
            return AsFunction<HWC2_PFN_GET_DOZE_SUPPORT>(&GetDozeSupport);
        case HWC2_FUNCTION_GET_HDR_CAPABILITIES:
            return AsFunction<HWC2_PFN_GET_HDR_CAPABILITIES>(&GetHdrCapabilities);
        case HWC2_FUNCTION_GET_RELEASE_FENCES:
            return AsFunction<HWC2_PFN_GET_RELEASE_FENCES>(&GetReleaseFences);
        case HWC2_FUNCTION_PRESENT_DISPLAY:
            return AsFunction<HWC2_PFN_PRESENT_DISPLAY>(&PresentDisplay);
        case HWC2_FUNCTION_SET_ACTIVE_CONFIG:
            return AsFunction<HWC2_PFN_SET_ACTIVE_CONFIG>(&SetActiveConfig);
        case HWC2_FUNCTION_SET_CLIENT_TARGET:
            return AsFunction<HWC2_PFN_SET_CLIENT_TARGET>(&SetClientTarget);
        case HWC2_FUNCTION_SET_COLOR_MODE:
            return AsFunction<HWC2_PFN_SET_COLOR_MODE>(&SetColorMode);
        case HWC2_FUNCTION_SET_COLOR_TRANSFORM:
            return AsFunction<HWC2_PFN_SET_COLOR_TRANSFORM>(&SetColorTransform);
        case HWC2_FUNCTION_SET_OUTPUT_BUFFER:
            return AsFunction<HWC2_PFN_SET_OUTPUT_BUFFER>(&SetOutputBuffer);
        case HWC2_FUNCTION_SET_POWER_MODE:
            return AsFunction<HWC2_PFN_SET_POWER_MODE>(&SetPowerMode);
        case HWC2_FUNCTION_SET_VSYNC_ENABLED:
            return AsFunction<HWC2_PFN_SET_VSYNC_ENABLED>(&SetVsyncEnabled);
        case HWC2_FUNCTION_VALIDATE_DISPLAY:
            return AsFunction<HWC2_PFN_VALIDATE_DISPLAY>(&ValidateDisplay);
        case HWC2_FUNCTION_GET_CLIENT_TARGET_SUPPORT:
            return AsFunction<HWC2_PFN_GET_CLIENT_TARGET_SUPPORT>(&GetClientTargetSupport);
        case HWC2_FUNCTION_GET_DISPLAY_IDENTIFICATION_DATA:
            return AsFunction<HWC2_PFN_GET_DISPLAY_IDENTIFICATION_DATA>(
                    &GetDisplayIdentificationData);
        case HWC2_FUNCTION_GET_DISPLAY_CAPABILITIES:
            return AsFunction<HWC2_PFN_GET_DISPLAY_CAPABILITIES>(&GetDisplayCapabilities);
        case HWC2_FUNCTION_GET_DISPLAY_BRIGHTNESS_SUPPORT:
            return AsFunction<HWC2_PFN_GET_DISPLAY_BRIGHTNESS_SUPPORT>(
                    &GetDisplayBrightnessSupport);
        case HWC2_FUNCTION_SET_DISPLAY_BRIGHTNESS:
            return AsFunction<HWC2_PFN_SET_DISPLAY_BRIGHTNESS>(&SetDisplayBrightness);
        case HWC2_FUNCTION_GET_DISPLAY_CONNECTION_TYPE:
            return AsFunction<HWC2_PFN_GET_DISPLAY_CONNECTION_TYPE>(&GetDisplayConnectionType);
        case HWC2_FUNCTION_GET_DISPLAY_VSYNC_PERIOD:
            return AsFunction<HWC2_PFN_GET_DISPLAY_VSYNC_PERIOD>(&GetDisplayVsyncPeriod);
        case HWC2_FUNCTION_SET_ACTIVE_CONFIG_WITH_CONSTRAINTS:
            return AsFunction<HWC2_PFN_SET_ACTIVE_CONFIG_WITH_CONSTRAINTS>(
                    &SetActiveConfigWithConstraints);
        case HWC2_FUNCTION_SET_CURSOR_POSITION:
            return AsFunction<HWC2_PFN_SET_CURSOR_POSITION>(&SetCursorPosition);
        case HWC2_FUNCTION_SET_LAYER_BUFFER:
            return AsFunction<HWC2_PFN_SET_LAYER_BUFFER>(&SetLayerBuffer);
        case HWC2_FUNCTION_SET_LAYER_SURFACE_DAMAGE:
            return AsFunction<HWC2_PFN_SET_LAYER_SURFACE_DAMAGE>(&SetLayerSurfaceDamage);
        case HWC2_FUNCTION_SET_LAYER_BLEND_MODE:
            return AsFunction<HWC2_PFN_SET_LAYER_BLEND_MODE>(&SetLayerBlendMode);
        case HWC2_FUNCTION_SET_LAYER_COLOR:
            return AsFunction<HWC2_PFN_SET_LAYER_COLOR>(&SetLayerColor);
        case HWC2_FUNCTION_SET_LAYER_COMPOSITION_TYPE:
            return AsFunction<HWC2_PFN_SET_LAYER_COMPOSITION_TYPE>(&SetLayerCompositionType);
        case HWC2_FUNCTION_SET_LAYER_DATASPACE:
            return AsFunction<HWC2_PFN_SET_LAYER_DATASPACE>(&SetLayerDataspace);
        case HWC2_FUNCTION_SET_LAYER_DISPLAY_FRAME:
            return AsFunction<HWC2_PFN_SET_LAYER_DISPLAY_FRAME>(&SetLayerDisplayFrame);
        case HWC2_FUNCTION_SET_LAYER_PLANE_ALPHA:
            return AsFunction<HWC2_PFN_SET_LAYER_PLANE_ALPHA>(&SetLayerPlaneAlpha);
        case HWC2_FUNCTION_SET_LAYER_SIDEBAND_STREAM:
            return AsFunction<HWC2_PFN_SET_LAYER_SIDEBAND_STREAM>(&SetLayerSidebandStream);
        case HWC2_FUNCTION_SET_LAYER_SOURCE_CROP:
            return AsFunction<HWC2_PFN_SET_LAYER_SOURCE_CROP>(&SetLayerSourceCrop);
        case HWC2_FUNCTION_SET_LAYER_TRANSFORM:
            return AsFunction<HWC2_PFN_SET_LAYER_TRANSFORM>(&SetLayerTransform);
        case HWC2_FUNCTION_SET_LAYER_VISIBLE_REGION:
            return AsFunction<HWC2_PFN_SET_LAYER_VISIBLE_REGION>(&SetLayerVisibleRegion);
        case HWC2_FUNCTION_SET_LAYER_Z_ORDER:
            return AsFunction<HWC2_PFN_SET_LAYER_Z_ORDER>(&SetLayerZOrder);
        default:
            return nullptr;
    }
}

int32_t HwcDevice::RegisterCallback(int32_t descriptor, hwc2_callback_data_t callbackData,
                                    hwc2_function_pointer_t pointer) {
    switch (descriptor) {
        case HWC2_CALLBACK_HOTPLUG:
        case HWC2_CALLBACK_REFRESH:
        case HWC2_CALLBACK_VSYNC:
        case HWC2_CALLBACK_VSYNC_2_4:
        case HWC2_CALLBACK_VSYNC_PERIOD_TIMING_CHANGED:
        case HWC2_CALLBACK_SEAMLESS_POSSIBLE:
            break;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }

    {
        std::lock_guard lock(callback_mutex_);
        if (pointer == nullptr) {
            callbacks_.erase(descriptor);
        } else {
            callbacks_[descriptor] = {callbackData, pointer};
        }
    }

    if (descriptor == HWC2_CALLBACK_HOTPLUG && pointer != nullptr) {
        const auto hotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer);
        for (const hwc2_display_t id : registry_->ConnectedDisplayIds()) {
            hotplug(callbackData, id, HWC2_CONNECTION_CONNECTED);
        }
    }
    return HWC2_ERROR_NONE;
}

std::string HwcDevice::Dump() const {
    std::string output =
            "Floral HWC2.4\n"
            "  virtual_displays: unsupported\n"
            "  readback: unsupported\n"
            "  encoding: detached\n";
    if (registry_ != nullptr) {
        for (const hwc2_display_t id : registry_->ConnectedDisplayIds()) {
            Display* display = registry_->Get(id);
            if (display != nullptr) {
                output += display->Dump();
            }
        }
    }
    return output;
}

DisplayConfig HwcDevice::LoadPrimaryDisplayConfig() {
    DisplayConfig config;
    config.width = BoundedProperty("ro.boot.redroid_width", 1920, 320, 7680);
    config.height = BoundedProperty("ro.boot.redroid_height", 1080, 320, 4320);
    config.supported_refresh_rates_hz = {15, 30, 60};
    const uint32_t requestedFramesPerSecond = BoundedProperty("ro.boot.redroid_fps", 60, 1, 60);
    const uint32_t selectedFramesPerSecond =
            SelectRefreshRateAtLeast(config.supported_refresh_rates_hz, requestedFramesPerSecond);
    config.dpi = BoundedProperty("ro.boot.redroid_dpi", 320, 72, 640);
    config.vsync_period_nanos = RefreshRateToVsyncPeriodNanos(selectedFramesPerSecond);
    return config;
}

void HwcDevice::EmitVsync(hwc2_display_t display, int64_t timestamp, int64_t periodNanos) {
    CallbackEntry callback;
    bool callback24 = false;
    {
        std::lock_guard lock(callback_mutex_);
        auto found = callbacks_.find(HWC2_CALLBACK_VSYNC_2_4);
        if (found != callbacks_.end()) {
            callback = found->second;
            callback24 = true;
        } else {
            found = callbacks_.find(HWC2_CALLBACK_VSYNC);
            if (found != callbacks_.end()) {
                callback = found->second;
            }
        }
    }

    if (callback.pointer == nullptr) {
        return;
    }
    if (callback24) {
        const auto vsync = reinterpret_cast<HWC2_PFN_VSYNC_2_4>(callback.pointer);
        vsync(callback.data, display, timestamp, periodNanos);
    } else {
        const auto vsync = reinterpret_cast<HWC2_PFN_VSYNC>(callback.pointer);
        vsync(callback.data, display, timestamp);
    }
}

}  // namespace floral::display
