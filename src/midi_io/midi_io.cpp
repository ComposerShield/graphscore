// SPDX-License-Identifier: Apache-2.0

#include <graphscore/midi_io/graphscore_midi_io.hpp>

namespace graphscore {
namespace {
constexpr int kMidiIOVersion = 1;
}  // namespace

int midi_io_version() { return kMidiIOVersion; }
}  // namespace graphscore
