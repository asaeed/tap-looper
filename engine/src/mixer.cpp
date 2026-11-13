#include "mixer.h"
#include <algorithm>
#include <cmath>

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

// Map an output-frame position within a play window to a PCM read position:
// scale by speed, rotate by start_offset, wrap into [0, sample_len).
inline float pcm_position(Frames local_out, float speed, int32_t start_offset, Frames sample_len) {
    float p = float(local_out) * speed + float(start_offset);
    p = std::fmod(p, float(sample_len));
    if (p < 0.f) p += float(sample_len);
    return p;
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
         uint64_t play_head,
         int frame_count,
         float* out,
         int extra_latency_frames) {
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

        // Grid offset for this sample. Non-master samples also shift by
        // extra_latency_frames so the user can dial alignment live; the result
        // is always normalized into [0, bar_frames) because SetMasterBarFrames
        // can shrink the bar below a previously recorded offset.
        int64_t L64 = int64_t(bar_frames);
        int64_t shifted = int64_t(s.recorded_offset_in_bar);
        if (!s.is_master_loop) shifted -= int64_t(extra_latency_frames);
        Frames offset = Frames(((shifted % L64) + L64) % L64);

        Frames sample_len = s.recorded_length_frames;
        float speed = speed_ratio(s.speed);
        // One full pass through the PCM measured in OUTPUT frames: at 2x speed
        // the sample finishes in half its recorded length; at 1/2x it takes double.
        Frames play_len = Frames(float(sample_len) / speed);
        if (play_len == 0) play_len = 1;

        if (s.repeat) {
            // Tile the sample continuously across the bar starting from offset.
            Frames tile_len = (play_len < bar_frames) ? play_len : bar_frames;
            for (int i = 0; i < frame_count; ++i) {
                Frames bar_pos = Frames((play_head + uint64_t(i)) % bar_frames);
                Frames anchored = (bar_pos + bar_frames - offset) % bar_frames;
                Frames local = anchored % tile_len;
                float pos = pcm_position(local, speed, s.start_offset_frames, sample_len);
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_lerp(s, pos, c) * gain;
                }
            }
        } else if (sample_len <= bar_frames) {
            // Sub-bar: plays once per bar in a window of play_len output frames
            // (speed-adjusted), with wrap handling at the bar boundary.
            Frames window = (play_len < bar_frames) ? play_len : bar_frames;
            for (int i = 0; i < frame_count; ++i) {
                Frames bar_pos = Frames((play_head + uint64_t(i)) % bar_frames);
                bool inside = false;
                Frames local = 0;
                if (offset + window <= bar_frames) {
                    if (bar_pos >= offset && bar_pos < offset + window) {
                        inside = true; local = bar_pos - offset;
                    }
                } else {
                    if (bar_pos >= offset) {
                        inside = true; local = bar_pos - offset;
                    } else if (bar_pos < (offset + window) % bar_frames) {
                        inside = true; local = (bar_frames - offset) + bar_pos;
                    }
                }
                if (!inside) continue;
                float pos = pcm_position(local, speed, s.start_offset_frames, sample_len);
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_lerp(s, pos, c) * gain;
                }
            }
        } else {
            // Multi-bar: iteration length snaps to the nearest bar multiple of the
            // sample's effective (speed-adjusted) play length. Content is anchored
            // at the timeline position where recording started (mod loop_len), so
            // it plays back at the exact grid position it was performed at —
            // anchoring at bar 0 rotated overdubs by their start position.
            Frames loop_len = nearest_multiple_of_L(play_len, bar_frames);
            int64_t anchor64 = s.recorded_start_total;
            if (!s.is_master_loop) anchor64 -= int64_t(extra_latency_frames);
            int64_t ll = int64_t(loop_len);
            Frames anchor = Frames(((anchor64 % ll) + ll) % ll);
            for (int i = 0; i < frame_count; ++i) {
                Frames absolute = Frames((play_head + uint64_t(i)) % loop_len);
                Frames local = (absolute + loop_len - anchor) % loop_len;
                if (local >= play_len) continue; // trailing silence within iteration
                float pos = pcm_position(local, speed, s.start_offset_frames, sample_len);
                for (int c = 0; c < kChannels; ++c) {
                    out[i * kChannels + c] += read_sample_lerp(s, pos, c) * gain;
                }
            }
        }
    }
}

} // namespace tl
