#pragma once
#include "types.h"

namespace tl {
// Derive BPM from master bar length, assumed beats per bar, and sample rate.
// Returns 0 if bar_frames is 0.
double derive_bpm(Frames bar_frames, int beats_per_bar, int sample_rate);
} // namespace tl
