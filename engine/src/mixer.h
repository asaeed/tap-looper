#pragma once
#include "sample.h"
#include "state_snapshot.h"
#include <vector>

namespace tl {

// Mix sample playback into `out` (interleaved stereo float, kChannels-wide).
// - `samples`: pointers to all current samples (the engine owns them).
// - `banks`: the engine's BankState array.
// - `bar_frames`: master bar length in frames (0 = no L yet, mixer outputs silence).
// - `play_head`: current global play head (0 .. bar_frames-1) at the START of this block.
// - `frame_count`: number of stereo frames to mix into `out`.
void mix(const std::vector<Sample*>& samples,
         const BankState* banks,
         Frames bar_frames,
         Frames play_head,
         int frame_count,
         float* out,
         int extra_latency_frames = 0);

Frames nearest_multiple_of_L(Frames sample_len, Frames L);

} // namespace tl
