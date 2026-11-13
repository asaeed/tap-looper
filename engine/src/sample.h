#pragma once
#include "state_snapshot.h"
#include "types.h"
#include <vector>

namespace tl {

struct Sample {
    // Immutable after recording
    SampleId id;
    Frames recorded_length_frames;
    Frames recorded_offset_in_bar;
    // Absolute timeline position where recording started (latency-compensated,
    // can be negative right after session start). Multi-bar playback anchors
    // to this mod loop_len so content plays at the grid position it was
    // performed at; recorded_offset_in_bar is this mod L.
    int64_t recorded_start_total;
    std::vector<float> pcm;             // interleaved, channels-wide

    // Mutable
    BankId bank;
    float volume;
    bool reverse;
    Speed speed;
    bool enabled;       // LOOP mode: whether loop is playing
    bool playing;       // ONE-SHOT: currently firing; or LOOP: currently audible iteration
    bool recording;
    Frames one_shot_play_pos;  // current playback position when ONE-SHOT is firing; advances each block
    float position_x, position_y;

    int32_t start_offset_frames;   // can be negative; rotates PCM at playback (LOOP) or shifts start position (ONE-SHOT)
    bool repeat;                   // sub-bar LOOP: tile across the whole bar instead of playing once

    int channels;
    Frames capacity_frames;

    bool is_master_loop;

    float env[kEnvBins];   // waveform envelope (peak per bin), computed once after recording

    Sample(SampleId id_, BankId bank_, Frames capacity_frames_, int channels_);

    // Append frames into pcm during recording. Stops at capacity.
    void append(const float* in, Frames frame_count);

    // Compute the peak-per-bin envelope from the recorded PCM. Call after
    // the recording is finalized (trims/fades applied).
    void compute_env();
};

} // namespace tl
