// SPDX-License-Identifier: Apache-2.0

#include <graphscore/writer_audio/graphscore_writer_audio.hpp>

namespace graphscore {
namespace {
constexpr int kWriterAudioVersion = 1;
}  // namespace

int writer_audio_version() {
  return kWriterAudioVersion;
}
}  // namespace graphscore
