// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

// SDL3 forward declaration (no SDL include in header)
struct SDL_Window;

namespace spike {

struct NativeHandleReport {
    bool valid = false;
    std::string platform;
    std::string handleType;
    bool handleNonNull = false;
    std::string error;
};

// Probe the native window handle through SDL3 properties API.
// Never dereferences the handle in portable code.
NativeHandleReport ProbeNativeHandle(SDL_Window* window);

} // namespace spike
