#include "mixer.h"
#include <algorithm>

namespace tl {

namespace {
inline float read_sample_lerp(const Sample& s, float position, int channel) {
    if (position < 0.f) position = 0.f;
    Frames len = s.recorded_length_frames;
    if (len == 0) return 0.f;
    if (position >= float(len - 1)) position = float(len - 1);
    if (s.reverse) position = float(len - 1) - position;
    Frames i0 = Frames(position);
    Frames i1 = std::min<Frames>(i0 + 1, len - 1);
    float frac = position - float(i0);
    float a = s.pcm[i0 * s.channels + channel];
    float b = s.pcm[i1 * s.channels + channel];
    return a + (b - a) * frac;
}

inline float speed_ratio(Speed sp) {
    switch (sp) {
    case Speed::Half:   return 0.5f;
    case Speed::Double: return 2.0f;
    default:            return 1.0f;
    }
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

    bool any_solo = false;
    for (int i = 0; i < kNumBanks; ++i) {
        if (banks[i].soloed) { any_solo = true; break; }
    }

    for (Sample* sp : samples) {
        if (!sp) continue;
        const Sample& s = *sp;
        const BankState& b = banks[s.bank];
        if (any_solo) {
            if (!b.soloed) continue;  // when any bank is soloed, only soloed banks play
        } else {
            if (b.muted) continue;    // normal mute behavior
        }
        if (s.recorded_length_frames == 0) continue;
        float gain = s.volume * b.volume;
        bool is_loop = (b.mode == BankMode::Loop);
        if (!is_loop) {
            // ONE-SHOT: play from one_shot_play_pos when playing=true.
            if (!s.playing) continue;
            Sample& sm = const_cast<Sample&>(s);   // mutable advance (RT thread is the only writer)
            float pos = float(sm.one_shot_play_pos);
            float rate = speed_ratio(sm.speed);
            for (int i = 0; i < frame_count; ++i) {
                if (pos >= float(sm.recorded_length_frames)) { sm.playing = false; break; }
                float read_pos = pos + float(sm.start_offset_frames);
                if (read_pos < 0.f) read_pos = 0.f;
                if (read_pos >= float(sm.recorded_length_frames)) { pos += rate; continue; } // silence past end
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_lerp(sm, read_pos, c) * gain;
                }
                pos += rate;
            }
            sm.one_shot_play_pos = Frames(pos);
            continue;
        }
        if (!s.enabled) continue;

        // For sub-bar samples, fire at `recorded_offset_in_bar` every bar.
        Frames offset = s.recorded_offset_in_bar;
        Frames sample_len = s.recorded_length_frames;

        if (s.repeat) {
            // Tile the sample across the bar. If sample is longer than the bar,
            // trim the overflow (only the first bar_frames worth of PCM is used).
            Frames tile_len = (sample_len < bar_frames) ? sample_len : bar_frames;
            if (tile_len == 0) continue;
            for (int i = 0; i < frame_count; ++i) {
                Frames bar_pos = (play_head + Frames(i)) % bar_frames;
                Frames anchored = (bar_pos + bar_frames - offset) % bar_frames;
                Frames local = anchored % tile_len;
                // Apply start_offset rotation
                int32_t off = s.start_offset_frames;
                int64_t shifted = int64_t(local) + int64_t(off);
                int64_t len = int64_t(tile_len);
                int64_t mod = ((shifted % len) + len) % len;
                Frames effective_local = Frames(mod);
                float pos = float(effective_local) * speed_ratio(s.speed);
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_lerp(s, pos, c) * gain;
                }
            }
        } else if (sample_len <= bar_frames) {
            // Sub-bar: existing window logic (unchanged)
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
                // Apply the per-sample start offset (rotates the playback within the PCM)
                int32_t off = s.start_offset_frames;
                Frames effective_local;
                if (s.recorded_length_frames == 0) {
                    effective_local = 0;
                } else {
                    int64_t shifted = int64_t(local) + int64_t(off);
                    int64_t len = int64_t(s.recorded_length_frames);
                    int64_t mod = ((shifted % len) + len) % len;
                    effective_local = Frames(mod);
                }
                float pos = float(effective_local) * speed_ratio(s.speed);
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_lerp(s, pos, c) * gain;
                }
            }
        } else {
            // Multi-bar: loop_length = nearest_multiple_of_L (unchanged)
            Frames loop_len = nearest_multiple_of_L(sample_len, bar_frames);
            // The sample's iteration starts at bar 0 of the master grid (offset 0 by spec).
            // We need a position within the loop iteration: loop_pos in [0, loop_len).
            for (int i = 0; i < frame_count; ++i) {
                Frames absolute = (play_head + Frames(i)) % loop_len;
                if (absolute >= sample_len) continue; // trailing silence within iteration
                Frames local = absolute; // reverse handled by read_sample_lerp
                // Apply the per-sample start offset (rotates the playback within the PCM)
                int32_t off = s.start_offset_frames;
                Frames effective_local;
                if (s.recorded_length_frames == 0) {
                    effective_local = 0;
                } else {
                    int64_t shifted = int64_t(local) + int64_t(off);
                    int64_t len = int64_t(s.recorded_length_frames);
                    int64_t mod = ((shifted % len) + len) % len;
                    effective_local = Frames(mod);
                }
                float pos = float(effective_local) * speed_ratio(s.speed);
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_lerp(s, pos, c) * gain;
                }
            }
        }
    }
}

} // namespace tl
