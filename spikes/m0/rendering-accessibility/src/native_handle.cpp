// SPDX-License-Identifier: Apache-2.0

#include "native_handle.hpp"
#include <SDL3/SDL.h>
#include <cstdio>

namespace spike {

NativeHandleReport ProbeNativeHandle(SDL_Window* window) {
    NativeHandleReport report;
    if (!window) {
        report.error = "null window";
        report.platform = "unknown";
        return report;
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    const char* platformName = SDL_GetPlatform();

    report.platform = platformName ? platformName : "unknown";

    if (report.platform == "macOS" || report.platform == "Mac OS X") {
        void* nsWindow = SDL_GetPointerProperty(props,
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        report.handleType = "NSWindow*";
        report.handleNonNull = (nsWindow != nullptr);
        report.valid = report.handleNonNull;
        std::fprintf(stdout, "[native-handle] platform=%s type=NSWindow* nonNull=%s valid=%s ptr=%p\n",
                     report.platform.c_str(),
                     report.handleNonNull ? "yes" : "no",
                     report.valid ? "yes" : "no",
                     nsWindow);
        if (!report.valid) {
            report.error = "NSWindow* is null — handle unavailable";
        }
    }
    else if (report.platform == "Windows") {
        void* hwnd = SDL_GetPointerProperty(props,
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        report.handleType = "HWND";
        report.handleNonNull = (hwnd != nullptr);
        report.valid = report.handleNonNull;
        std::fprintf(stdout, "[native-handle] platform=%s type=HWND nonNull=%s valid=%s ptr=%p\n",
                     report.platform.c_str(),
                     report.handleNonNull ? "yes" : "no",
                     report.valid ? "yes" : "no",
                     hwnd);
        if (!report.valid) {
            report.error = "HWND is null — handle unavailable";
        }
    }
    else if (report.platform == "Linux") {
        // Try Wayland surface first
        void* surface = SDL_GetPointerProperty(props,
            SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (surface) {
            report.handleType = "wl_surface* (Wayland)";
            report.handleNonNull = true;
            report.valid = true;
            std::fprintf(stdout, "[native-handle] platform=%s type=wl_surface* nonNull=yes valid=yes ptr=%p\n",
                         report.platform.c_str(), surface);
        } else {
            // Try X11 — X11 property is a number (Window = uint32_t), not a pointer
            Sint64 x11Win = SDL_GetNumberProperty(props,
                SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
            report.handleType = "Window (X11, uint32_t)";
            report.handleNonNull = (x11Win != 0);
            report.valid = report.handleNonNull;
            std::fprintf(stdout, "[native-handle] platform=%s type=Window(X11) nonNull=%s valid=%s id=%lld\n",
                         report.platform.c_str(),
                         report.handleNonNull ? "yes" : "no",
                         report.valid ? "yes" : "no",
                         static_cast<long long>(x11Win));
            if (!report.valid) {
                report.error = "Neither Wayland surface nor X11 Window found — handle unavailable";
            }
        }
    }
    else {
        report.error = "unsupported platform: " + report.platform;
        report.valid = false;
    }

    return report;
}

} // namespace spike
