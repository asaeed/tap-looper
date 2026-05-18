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

    int channels;
    Frames capacity_frames;

    Sample(SampleId id_, BankId bank_, Frames capacity_frames_, int channels_);

    // Append frames into pcm during recording. Stops at capacity.
    void append(const float* in, Frames frame_count);
};

} // namespace tl
