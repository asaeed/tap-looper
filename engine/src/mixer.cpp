#include "mixer.h"
#include <algorithm>

namespace tl {

namespace {
inline float read_sample_frame(const Sample& s, Frames frame, int channel) {
    Frames idx = s.reverse ? (s.recorded_length_frames - 1 - frame) : frame;
    return s.pcm[idx * s.channels + channel];
}
} // namespace

void mix(const std::vector<Sample*>& samples,
         const BankState* banks,
         Frames bar_frames,
         Frames play_head,
         int frame_count,
         float* out) {
    if (bar_frames == 0) return; // nothing to do, output is pre-zeroed by caller

    for (Sample* sp : samples) {
        if (!sp) continue;
        const Sample& s = *sp;
        const BankState& b = banks[s.bank];
        if (b.muted) continue;
        bool is_loop = (b.mode == BankMode::Loop);
        if (!is_loop) continue;          // ONE-SHOT handled in Task 15
        if (!s.enabled) continue;
        if (s.recorded_length_frames == 0) continue;

        float gain = s.volume * b.volume;

        // For sub-bar samples, fire at `recorded_offset_in_bar` every bar.
        Frames offset = s.recorded_offset_in_bar;
        Frames sample_len = s.recorded_length_frames;
        // For sub-bar: scheduled window is [offset, offset + sample_len) in bar coords.

        for (int i = 0; i < frame_count; ++i) {
            Frames bar_pos = (play_head + Frames(i)) % bar_frames;
            // Determine if we're inside the sample's play window.
            // Window may wrap across bar boundary if offset + sample_len > bar_frames.
            bool inside = false;
            Frames local = 0;
            if (offset + sample_len <= bar_frames) {
                if (bar_pos >= offset && bar_pos < offset + sample_len) {
                    inside = true; local = bar_pos - offset;
                }
            } else {
                // wrapping window
                if (bar_pos >= offset) {
                    inside = true; local = bar_pos - offset;
                } else if (bar_pos < (offset + sample_len) % bar_frames) {
                    inside = true; local = (bar_frames - offset) + bar_pos;
                }
            }
            if (!inside) continue;
            // local in [0, sample_len)
            for (int c = 0; c < kChannels; ++c) {
                out[i * kChannels + c] += read_sample_frame(s, local, c) * gain;
            }
        }
    }
}

} // namespace tl
