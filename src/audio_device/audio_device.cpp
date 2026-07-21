// SPDX-License-Identifier: Apache-2.0

#include <graphscore/audio_device/graphscore_audio_device.hpp>

namespace graphscore {
namespace {
constexpr int kAudioDeviceVersion = 1;
}  // namespace

int audio_device_version() { return kAudioDeviceVersion; }
}  // namespace graphscore
