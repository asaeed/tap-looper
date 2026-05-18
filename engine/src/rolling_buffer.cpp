#include "rolling_buffer.h"
#include <cassert>
#include <cstring>

namespace tl {

RollingBuffer::RollingBuffer(Frames capacity_frames, int channels)
    : capacity_frames_(capacity_frames),
      channels_(channels),
      data_(capacity_frames * channels, 0.f),
      write_pos_(0),
      wrapped_(false) {}

void RollingBuffer::write(const float* in, Frames frame_count) {
    for (Frames i = 0; i < frame_count; ++i) {
        for (int c = 0; c < channels_; ++c) {
            data_[write_pos_ * channels_ + c] = in[i * channels_ + c];
        }
        write_pos_++;
        if (write_pos_ >= capacity_frames_) {
            write_pos_ = 0;
            wrapped_ = true;
        }
    }
}

void RollingBuffer::read_last(float* out, Frames frame_count) const {
    assert(frame_count <= capacity_frames_);
    // Compute start position so the last `frame_count` written frames are read in order.
    // current write_pos_ points to the NEXT slot to write. The last frame is at write_pos_-1.
    Frames start = (write_pos_ + capacity_frames_ - frame_count) % capacity_frames_;
    for (Frames i = 0; i < frame_count; ++i) {
        Frames pos = (start + i) % capacity_frames_;
        for (int c = 0; c < channels_; ++c) {
            out[i * channels_ + c] = data_[pos * channels_ + c];
        }
    }
}

} // namespace tl
