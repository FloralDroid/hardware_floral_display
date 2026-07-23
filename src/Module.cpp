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

#include <hardware/hardware.h>
#include <hardware/hwcomposer2.h>
#include <log/log.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <new>

namespace floral::display {
namespace {

int CloseDevice(hw_device_t* device) {
    delete HwcDevice::From(reinterpret_cast<hwc2_device_t*>(device));
    return 0;
}

int OpenDevice(const hw_module_t* module, const char* name, hw_device_t** outDevice) {
    if (name == nullptr || outDevice == nullptr || std::strcmp(name, HWC_HARDWARE_COMPOSER) != 0) {
        return -EINVAL;
    }

    std::unique_ptr<HwcDevice> device(new (std::nothrow) HwcDevice());
    if (device == nullptr) {
        return -ENOMEM;
    }
    device->common.module = const_cast<hw_module_t*>(module);
    device->common.close = CloseDevice;
    *outDevice = &device.release()->common;
    ALOGI("Floral HWC2.4 device opened");
    return 0;
}

hw_module_methods_t kModuleMethods = {
        .open = OpenDevice,
};

}  // namespace
}  // namespace floral::display

extern "C" {

hw_module_t HAL_MODULE_INFO_SYM = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = HARDWARE_MODULE_API_VERSION(2, 4),
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = HWC_HARDWARE_MODULE_ID,
        .name = "Floral HWC2.4 physical display module",
        .author = "FloralDroid",
        .methods = &floral::display::kModuleMethods,
        .dso = nullptr,
        .reserved = {0},
};

}  // extern "C"
