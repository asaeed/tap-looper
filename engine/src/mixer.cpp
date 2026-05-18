#include "mixer.h"
#include <algorithm>

namespace tl {

namespace {
inline float read_sample_frame(const Sample& s, Frames frame, int channel) {
    Frames idx = s.reverse ? (s.recorded_length_frames - 1 - frame) : frame;
    return s.pcm[idx * s.channels + channel];
}
} // namespace

Frames nearest_multiple_of_L(Frames sample_len, Frames L) {
    if (L == 0) return 0;
    if (sample_len <= L) return L; // sub-bar handled separately; this is the "snap" used for multi-bar
    // Multi-bar minimum = 2L.
    Frames lower = (sample_len / L) * L;
    Frames upper = lower + L;
    if (lower < 2 * L) lower = 2 * L;
    if (upper < 2 * L) upper = 2 * L;
    Frames lower_diff = (sample_len > lower) ? (sample_len - lower) : (lower - sample_len);
    Frames upper_diff = upper - sample_len;
    return (upper_diff < lower_diff) ? upper : lower;
}

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

        if (sample_len <= bar_frames) {
            // Sub-bar: existing window logic (unchanged from Task 12)
            for (int i = 0; i < frame_count; ++i) {
                Frames bar_pos = (play_head + Frames(i)) % bar_frames;
                bool inside = false;
                Frames local = 0;
                if (offset + sample_len <= bar_frames) {
                    if (bar_pos >= offset && bar_pos < offset + sample_len) {
                        inside = true; local = bar_pos - offset;
                    }
                } else {
                    if (bar_pos >= offset) {
                        inside = true; local = bar_pos - offset;
                    } else if (bar_pos < (offset + sample_len) % bar_frames) {
                        inside = true; local = (bar_frames - offset) + bar_pos;
                    }
                }
                if (!inside) continue;
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_frame(s, local, c) * gain;
                }
            }
        } else {
            // Multi-bar: loop_length = nearest_multiple_of_L
            Frames loop_len = nearest_multiple_of_L(sample_len, bar_frames);
            // The sample's iteration starts at bar 0 of the master grid (offset 0 by spec).
            // We need a position within the loop iteration: loop_pos in [0, loop_len).
            for (int i = 0; i < frame_count; ++i) {
                Frames absolute = (play_head + Frames(i)) % loop_len;
                if (absolute >= sample_len) continue; // trailing silence within iteration
                Frames local = absolute; // reverse handled by read_sample_frame
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_frame(s, local, c) * gain;
                }
            }
        }
    }
}

} // namespace tl
