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

#include <hardware/gralloc.h>

#include <memory>

#include "floral/display/StreamFrameSink.h"

namespace floral::display {

// Resolves versioned allocator metadata first and keeps gralloc4 metadata as
// the standard fallback for newer platform branches.
std::shared_ptr<ClientTargetResolver> CreateGraphicBufferClientTargetResolver();

// Injectable module entry point used by allocator contract tests.
std::shared_ptr<ClientTargetResolver> CreateGraphicBufferClientTargetResolver(
        const gralloc_module_t* grallocModule);

}  // namespace floral::display
