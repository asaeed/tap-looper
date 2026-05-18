#pragma once
#include "types.h"
#include <cstddef>
#include <vector>

namespace tl {

// Single-writer, single-reader circular buffer for interleaved float audio.
// Used by the engine to retain the last N frames of input for "capture last loop".
// Writer = RT audio thread; reader = worker thread (or tests).
class RollingBuffer {
public:
    RollingBuffer(Frames capacity_frames, int channels);

    void write(const float* in, Frames frame_count);     // wraps around on overflow
    void read_last(float* out, Frames frame_count) const; // copies last N frames

    Frames capacity_frames() const { return capacity_frames_; }
    int channels() const { return channels_; }

private:
    Frames capacity_frames_;
    int channels_;
    std::vector<float> data_;
    Frames write_pos_;       // next frame to write
    bool wrapped_;
};

} // namespace tl
