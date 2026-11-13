#include "sample.h"
#include <algorithm>
#include <cstring>

namespace tl {

Sample::Sample(SampleId id_, BankId bank_, Frames capacity_frames_, int channels_)
    : id(id_),
      recorded_length_frames(0),
      recorded_offset_in_bar(0),
      recorded_start_total(0),
      pcm(size_t(capacity_frames_) * size_t(channels_), 0.f),
      bank(bank_),
      volume(1.0f),
      reverse(false),
      speed(Speed::Normal),
      enabled(false),
      playing(false),
      recording(false),
      one_shot_play_pos(0),
      position_x(0.f),
      position_y(0.f),
      start_offset_frames(0),
      repeat(false),
      channels(channels_),
      capacity_frames(capacity_frames_),
      is_master_loop(false) {
    for (int i = 0; i < kEnvBins; ++i) env[i] = 0.f;
}

void Sample::append(const float* in, Frames frame_count) {
    if (!recording) return;
    Frames available = capacity_frames - recorded_length_frames;
    Frames to_write = std::min(frame_count, available);
    if (to_write == 0) return;
    std::memcpy(pcm.data() + recorded_length_frames * channels,
                in,
                to_write * channels * sizeof(float));
    recorded_length_frames += to_write;
}

void Sample::compute_env() {
    for (int i = 0; i < kEnvBins; ++i) env[i] = 0.f;
    if (recorded_length_frames == 0) return;
    for (Frames f = 0; f < recorded_length_frames; ++f) {
        int bin = int(uint64_t(f) * kEnvBins / recorded_length_frames);
        for (int ch = 0; ch < channels; ++ch) {
            float v = pcm[f * channels + ch];
            if (v < 0) v = -v;
            if (v > env[bin]) env[bin] = v;
        }
    }
}

} // namespace tl
