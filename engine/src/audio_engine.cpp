#include "audio_engine.h"
#include "command_queue.h"
#include "rolling_buffer.h"
#include "sample.h"
#include "bpm.h"
#include "mixer.h"
#include "wav_writer.h"
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

namespace tl {

namespace {
// The UI polls at ~33 ms; publishing every callback (~2.7 ms at 128 frames)
// would be ~12x wasted state copies + a make_shared on the RT thread. State
// is published immediately when commands were applied or a recording is
// active, otherwise every Nth callback.
constexpr int kPublishEveryN = 8;

void mirror_sample(const Sample& s, SampleState& ss) {
    ss.id = s.id; ss.bank = s.bank;
    ss.volume = s.volume; ss.reverse = s.reverse; ss.speed = s.speed;
    ss.enabled = s.enabled; ss.playing = s.playing; ss.recording = s.recording;
    ss.position_x = s.position_x; ss.position_y = s.position_y;
    ss.recorded_length_frames = s.recorded_length_frames;
    ss.recorded_offset_in_bar = s.recorded_offset_in_bar;
    ss.start_offset_frames = s.start_offset_frames;
    ss.repeat = s.repeat;
    for (int i = 0; i < kEnvBins; ++i) ss.env[i] = s.env[i];
}

// mkdir -p equivalent (POSIX; macOS/Linux/Android).
void mkpath(const std::string& dir) {
    for (size_t i = 1; i < dir.size(); ++i) {
        if (dir[i] == '/') ::mkdir(dir.substr(0, i).c_str(), 0755);
    }
    ::mkdir(dir.c_str(), 0755);
}
} // namespace

struct AudioEngine::Impl {
    StatePublisher publisher;
    EngineState state;        // RT-thread-owned working copy
    CommandQueue<256> commands;
    RollingBuffer rolling{kFallbackBufferFrames, kChannels};
    std::vector<std::unique_ptr<Sample>> samples;
    std::unique_ptr<Sample> active_recording;
    SampleId next_id{1};
    int sample_rate{kSampleRate};
    // Unwrapped total frames since the master timeline started. Never wrapped
    // to the bar: multi-bar samples need a position spanning their full loop
    // length (wrapping mod L made them repeat their first bar forever).
    uint64_t global_play_head{0};
    int round_trip_latency_frames{0};
    int extra_latency_frames{0};  // by-ear LAT knob; round_trip already compensates measured latency

    std::vector<Sample*> mix_scratch; // reused every callback; no RT allocation
    int publish_tick{0};

    // Metronome click synth (sine burst with exponential decay)
    double click_phase{0.0};
    float click_env{0.f};
    float click_freq{1000.f};

    Impl() {
        mix_scratch.reserve(kMaxSamples);
        init_default_state(state);
        publisher.publish(state);
    }

    void apply(const Command& c) {
        switch (c.type) {
        case CommandType::StartRecording: {
            if (active_recording) return; // already recording, ignore
            if (samples.size() >= size_t(kMaxSamples)) return; // grid full; UI can't show more
            active_recording = std::make_unique<Sample>(
                next_id++, c.bank, kMaxRecFrames, kChannels);
            active_recording->position_x = c.fx;
            active_recording->position_y = c.fy;
            active_recording->recording = true;
            // recorded_offset_in_bar captured at recording start, with latency compensation
            if (state.master_bar_frames > 0) {
                // Compensate for round-trip latency: the audio the user hears now
                // was emitted ~output_latency ago, and their played notes arrive at
                // the engine ~input_latency late. Shifting the start backward by
                // round_trip_latency makes the playback land in sync with the loop.
                // extra_latency_frames is a user-tunable ±offset applied at mix time.
                int64_t start = int64_t(global_play_head) - int64_t(round_trip_latency_frames);
                active_recording->recorded_start_total = start;
                int64_t L = int64_t(state.master_bar_frames);
                active_recording->recorded_offset_in_bar = Frames(((start % L) + L) % L);
            }
            state.recording = true;
            state.recording_sample_id = active_recording->id;
            state.focused_bank = c.bank;   // CAPTURE targets the last-touched bank
            break;
        }
        case CommandType::StopRecording: {
            if (!active_recording) return;
            active_recording->recording = false;

            // The loop is exactly the span the user bounded: it starts when the
            // box is created (press-down) and ends at the stop press. No content
            // trimming — leading silence is performance space, and the old
            // leading/trailing onset trims moved the loop boundaries away from
            // the user's gestures.
            Sample& rec = *active_recording;

            // ── Quantize loop length to the bar grid ────────────────────────
            // A take longer than one bar plays back in whole-bar cycles, so a
            // 3.9-bar recording would wrap mid-phrase (or leave a silence gap)
            // at the seam. Snap the length to the nearest whole bar: truncate
            // the overhang, or pad the tail with silence (never restore
            // trimmed audio — that would bring the stop-click back).
            if (state.master_bar_frames > 0 &&
                rec.recorded_length_frames > state.master_bar_frames) {
                Frames L = state.master_bar_frames;
                Frames bars = (rec.recorded_length_frames + L / 2) / L;
                Frames target = bars * L;
                if (target > rec.capacity_frames) target = rec.capacity_frames;
                if (target > rec.recorded_length_frames) {
                    // Fade the real-content edge before padding so the
                    // audio→silence junction inside the loop doesn't click.
                    Frames fl = std::min<Frames>(96, rec.recorded_length_frames / 4);
                    for (Frames f = 0; f < fl; ++f) {
                        float gain = float(f + 1) / float(fl + 1);
                        Frames pos = rec.recorded_length_frames - 1 - f;
                        for (int ch = 0; ch < rec.channels; ++ch) {
                            rec.pcm[pos * rec.channels + ch] *= gain;
                        }
                    }
                    std::memset(rec.pcm.data() + size_t(rec.recorded_length_frames) * rec.channels,
                                0,
                                size_t(target - rec.recorded_length_frames) * rec.channels * sizeof(float));
                }
                std::fprintf(stderr,
                    "[engine] bar quantize: len=%u -> %u (%u bars of %u)\n",
                    (unsigned)rec.recorded_length_frames, (unsigned)target,
                    (unsigned)bars, (unsigned)L);
                rec.recorded_length_frames = target;
            }
            // ────────────────────────────────────────────────────────────────

            // ── Apply edge fades to eliminate click/pop at playback start
            //    and at the loop-wrap boundary (where end-of-loop meets the
            //    next start). 2 ms linear ramps on both ends. Capped to 25%
            //    of the recording length so short samples aren't dominated.
            {
                constexpr Frames kFadeBase = 96;   // 2 ms at 48 kHz
                Frames fade_len = std::min(kFadeBase, rec.recorded_length_frames / 4);
                if (fade_len > 0) {
                    for (Frames f = 0; f < fade_len; ++f) {
                        float gain = float(f + 1) / float(fade_len + 1);
                        for (int ch = 0; ch < rec.channels; ++ch) {
                            rec.pcm[f * rec.channels + ch] *= gain;
                        }
                    }
                    for (Frames f = 0; f < fade_len; ++f) {
                        float gain = float(f + 1) / float(fade_len + 1);
                        Frames pos = rec.recorded_length_frames - 1 - f;
                        for (int ch = 0; ch < rec.channels; ++ch) {
                            rec.pcm[pos * rec.channels + ch] *= gain;
                        }
                    }
                }
            }
            // ────────────────────────────────────────────────────────────────

            Frames len = active_recording->recorded_length_frames;
            Frames min_frames = Frames(sample_rate * kMinRecordingMs / 1000);
            if (len < min_frames) {
                active_recording.reset();
                state.recording = false;
                state.recording_duration_frames = 0;
                state.recording_sample_id = 0;
                break;
            }

            // Free the unused tail of the 60 s preallocation (23 MB per sample
            // otherwise). One-off allocation on the RT thread — same accepted
            // class as the trims above.
            rec.pcm.resize(size_t(rec.recorded_length_frames) * size_t(rec.channels));
            rec.pcm.shrink_to_fit();
            rec.capacity_frames = rec.recorded_length_frames;
            rec.compute_env();

            if (state.master_bar_frames == 0) {
                active_recording->is_master_loop = true;
                state.master_bar_frames = len;
                rolling = RollingBuffer(state.master_bar_frames, kChannels);
                // The stop press IS the loop boundary: playback begins
                // immediately at the top of the loop.
                global_play_head = 0;
            }
            // For a LOOP-mode bank, mark enabled so it starts playing.
            BankMode m = state.banks[active_recording->bank].mode;
            if (m == BankMode::Loop) {
                active_recording->enabled = true;
            }
            samples.push_back(std::move(active_recording));
            state.sample_count = int(samples.size());
            state.recording = false;
            state.recording_duration_frames = 0;
            state.recording_sample_id = 0;
            break;
        }
        case CommandType::TapSample: {
            for (auto& up : samples) {
                if (up->id != c.sample_id) continue;
                BankMode m = state.banks[up->bank].mode;
                if (m == BankMode::Loop) {
                    up->enabled = !up->enabled;
                } else {
                    up->one_shot_play_pos = 0;
                    up->playing = true;
                }
                break;
            }
            break;
        }
        case CommandType::FireAllInBank: {
            for (auto& up : samples) {
                if (up->bank != c.bank) continue;
                if (state.banks[c.bank].mode != BankMode::OneShot) continue;
                up->one_shot_play_pos = 0;
                up->playing = true;
            }
            break;
        }
        case CommandType::SetBankSolo:    state.banks[c.bank].soloed = c.bvalue; break;
        case CommandType::SetBankMute:    state.banks[c.bank].muted = c.bvalue; break;
        case CommandType::SetBankVolume:  state.banks[c.bank].volume = c.fvalue; break;
        case CommandType::SetBankMode: {
            BankMode new_mode = c.ivalue == 1 ? BankMode::OneShot : BankMode::Loop;
            if (state.banks[c.bank].mode != new_mode) {
                state.banks[c.bank].mode = new_mode;
                // Reset playback state for samples in this bank.
                // Looping samples stop; one-shots wait for explicit trigger.
                for (auto& up : samples) {
                    if (up->bank != c.bank) continue;
                    up->enabled = false;
                    up->playing = false;
                    up->one_shot_play_pos = 0;
                }
            }
            break;
        }

        case CommandType::SetSampleVolume: {
            for (auto& up : samples) if (up->id == c.sample_id) { up->volume = c.fvalue; break; }
            break;
        }
        case CommandType::SetSampleReverse: {
            for (auto& up : samples) if (up->id == c.sample_id) { up->reverse = c.bvalue; break; }
            break;
        }
        case CommandType::SetSampleSpeed: {
            for (auto& up : samples) if (up->id == c.sample_id) { up->speed = Speed(c.ivalue); break; }
            break;
        }
        case CommandType::DeleteSample: {
            samples.erase(std::remove_if(samples.begin(), samples.end(),
                [&](const std::unique_ptr<Sample>& s){ return s->id == c.sample_id; }), samples.end());
            state.sample_count = int(samples.size());
            break;
        }
        case CommandType::MoveSample: {
            for (auto& up : samples) {
                if (up->id != c.sample_id) continue;
                up->bank = c.bank;
                up->position_x = c.fx;
                up->position_y = c.fy;
                // Non-destructive: PCM and recorded_offset_in_bar untouched.
                break;
            }
            state.focused_bank = c.bank;
            break;
        }
        case CommandType::CaptureLastLoop: {
            if (state.master_bar_frames == 0) break; // no L yet
            if (samples.size() >= size_t(kMaxSamples)) break; // grid full
            Frames len = state.master_bar_frames;
            auto sample = std::make_unique<Sample>(next_id++, c.bank, len, kChannels);
            sample->position_x = 0.5f; sample->position_y = 0.5f;
            sample->recorded_start_total = int64_t(global_play_head) - int64_t(len);
            sample->recorded_offset_in_bar = Frames(global_play_head % uint64_t(len));
            sample->recording = true;
            std::vector<float> buf(size_t(len) * kChannels);
            rolling.read_last(buf.data(), len);
            sample->append(buf.data(), len);
            sample->recording = false;
            sample->compute_env();
            if (state.banks[c.bank].mode == BankMode::Loop) sample->enabled = true;
            samples.push_back(std::move(sample));
            state.sample_count = int(samples.size());
            state.focused_bank = c.bank;
            break;
        }
        case CommandType::SetBeatsPerBar: {
            state.beats_per_bar = c.ivalue;
            break;
        }
        case CommandType::Reset: {
            samples.clear();
            active_recording.reset();
            init_default_state(state);
            global_play_head = 0;
            next_id = 1;
            rolling = RollingBuffer(kFallbackBufferFrames, kChannels);
            break;
        }
        case CommandType::SetMasterPlaying:
            state.master_playing = c.bvalue;
            break;
        case CommandType::SetSampleStartOffset: {
            for (auto& up : samples) if (up->id == c.sample_id) { up->start_offset_frames = c.ivalue; break; }
            break;
        }
        case CommandType::SetSampleRepeat: {
            for (auto& up : samples) if (up->id == c.sample_id) { up->repeat = c.bvalue; break; }
            break;
        }
        case CommandType::SetMasterBarFrames: {
            if (c.ivalue <= 0) break;
            // Clamp to a sane range: 100ms minimum, 60s maximum
            Frames new_L = Frames(c.ivalue);
            Frames min_L = Frames(sample_rate / 10);          // 100ms
            Frames max_L = Frames(sample_rate * 60);          // 60s
            if (new_L < min_L) new_L = min_L;
            if (new_L > max_L) new_L = max_L;
            state.master_bar_frames = new_L;
            rolling = RollingBuffer(new_L, kChannels);
            // Rebase the timeline into the new bar
            global_play_head = global_play_head % uint64_t(new_L);
            break;
        }
        case CommandType::SetExtraLatencyFrames:
            extra_latency_frames = c.ivalue;
            state.extra_latency_frames = c.ivalue;
            break;
        case CommandType::CancelRecording: {
            if (!active_recording) break;
            active_recording.reset();
            state.recording = false;
            state.recording_duration_frames = 0;
            state.recording_sample_id = 0;
            break;
        }
        case CommandType::SetMetronome:
            state.metronome = c.bvalue;
            break;
        default: break; // other commands implemented in later tasks
        }
    }

    void publish_state() {
        bool any_solo = false;
        for (int i = 0; i < kNumBanks; ++i) {
            if (state.banks[i].soloed) { any_solo = true; break; }
        }
        // LOOP samples: 'playing' tracks enabled; audibility (mute/solo) is
        // applied at mirror time below. ONE-SHOT 'playing' is owned by the mixer.
        for (auto& up : samples) {
            if (state.banks[up->bank].mode == BankMode::Loop) up->playing = up->enabled;
        }

        // mirror samples vector into state.samples
        int n = std::min(int(samples.size()), kMaxSamples);
        for (int i = 0; i < n; ++i) {
            const Sample& s = *samples[i];
            mirror_sample(s, state.samples[i]);
            const BankState& b = state.banks[s.bank];
            bool audible = any_solo ? b.soloed : !b.muted;
            state.samples[i].playing = s.playing && audible;
        }
        // Also mirror the active recording (if any) so the UI shows the box during recording
        if (active_recording && n < kMaxSamples) {
            mirror_sample(*active_recording, state.samples[n]);
            n++;
        }
        state.sample_count = n;
        state.bpm = float(derive_bpm(state.master_bar_frames, state.beats_per_bar, sample_rate));
        state.extra_latency_frames = extra_latency_frames;
        state.version++;
        publisher.publish(state);
    }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() = default;

void AudioEngine::process(const float* in, float* out, int frame_count) {
    // 1. Drain commands
    Command c{};
    bool did_apply = false;
    while (impl_->commands.pop(c)) { impl_->apply(c); did_apply = true; }

    // 2. Track input peak (decaying) for the UI level meter
    {
        float peak = 0.f;
        const int n = frame_count * kChannels;
        for (int i = 0; i < n; ++i) {
            float v = in[i] < 0.f ? -in[i] : in[i];
            if (v > peak) peak = v;
        }
        impl_->state.input_peak = std::max(peak, impl_->state.input_peak * 0.85f);
    }

    // 3. Append input to rolling buffer
    impl_->rolling.write(in, Frames(frame_count));

    // 4. Append input to active recording if any; finalize when the 60s cap
    //    is hit instead of silently freezing the counter.
    if (impl_->active_recording) {
        impl_->active_recording->append(in, Frames(frame_count));
        impl_->state.recording_duration_frames = impl_->active_recording->recorded_length_frames;
        if (impl_->active_recording->recorded_length_frames >= kMaxRecFrames) {
            Command stop{}; stop.type = CommandType::StopRecording;
            impl_->apply(stop);
            did_apply = true;
        }
    }

    // 5. Zero output then mix (using current play_head)
    std::memset(out, 0, sizeof(float) * size_t(frame_count) * kChannels);
    if (impl_->state.master_playing) {
        impl_->mix_scratch.clear();   // capacity persists — no RT allocation
        for (auto& up : impl_->samples) impl_->mix_scratch.push_back(up.get());
        mix(impl_->mix_scratch, impl_->state.banks, impl_->state.master_bar_frames,
            impl_->global_play_head, frame_count, out, impl_->extra_latency_frames);
    }

    // 6. Metronome click on every beat of the master bar
    if (impl_->state.metronome && impl_->state.master_playing &&
        impl_->state.master_bar_frames > 0 && impl_->state.beats_per_bar > 0) {
        Frames L = impl_->state.master_bar_frames;
        Frames beat_len = L / Frames(impl_->state.beats_per_bar);
        if (beat_len > 0) {
            constexpr double kTwoPi = 6.283185307179586;
            for (int i = 0; i < frame_count; ++i) {
                Frames p = (impl_->global_play_head + Frames(i)) % L;
                // Skip the ragged remainder when L doesn't divide evenly.
                if (p % beat_len == 0 && p / beat_len < Frames(impl_->state.beats_per_bar)) {
                    impl_->click_env = 0.4f;
                    impl_->click_phase = 0.0;
                    impl_->click_freq = (p == 0) ? 1600.f : 1050.f;  // downbeat higher
                }
                if (impl_->click_env > 0.0005f) {
                    float v = float(std::sin(impl_->click_phase)) * impl_->click_env;
                    impl_->click_phase += kTwoPi * double(impl_->click_freq) / double(impl_->sample_rate);
                    impl_->click_env *= 0.995f;   // ~30ms decay
                    out[i * kChannels + 0] += v;
                    out[i * kChannels + 1] += v;
                }
            }
        }
    }

    // 7. Advance global play head (AFTER mix). Unwrapped total — the mixer
    //    takes positions mod bar_frames / loop_len itself.
    if (impl_->state.master_bar_frames > 0) {
        impl_->global_play_head += uint64_t(frame_count);
    }

    // 8. Publish state — immediately when commands ran or recording is active,
    //    otherwise every kPublishEveryN callbacks (see constant above).
    if (did_apply || impl_->active_recording || ++impl_->publish_tick >= kPublishEveryN) {
        impl_->publish_tick = 0;
        impl_->publish_state();
    }
}

bool AudioEngine::submit(const Command& cmd) {
    return impl_->commands.push(cmd);
}

EngineState AudioEngine::snapshot() const {
    return impl_->publisher.snapshot();
}

int AudioEngine::sample_rate() const { return impl_->sample_rate; }

void AudioEngine::set_round_trip_latency_frames(int frames) {
    impl_->round_trip_latency_frames = frames;
}

void AudioEngine::set_extra_latency_frames(int frames) {
    impl_->extra_latency_frames = frames;
}

int AudioEngine::export_session(const char* dir) {
    // PRECONDITION: audio I/O stopped — walks RT-owned sample data directly.
    std::string base(dir);
    mkpath(base);
    int written = 0;
    for (const auto& up : impl_->samples) {
        const Sample& s = *up;
        if (s.recorded_length_frames == 0) continue;
        char name[96];
        std::snprintf(name, sizeof(name), "/sample-%llu-bank%d.wav",
                      (unsigned long long)s.id, int(s.bank) + 1);
        if (write_wav_f32((base + name).c_str(), s.pcm.data(),
                          s.recorded_length_frames, s.channels, impl_->sample_rate)) {
            ++written;
        }
    }
    FILE* f = std::fopen((base + "/session.json").c_str(), "w");
    if (f) {
        const EngineState& st = impl_->state;
        std::fprintf(f,
            "{\n"
            "  \"sample_rate\": %d,\n"
            "  \"master_bar_frames\": %u,\n"
            "  \"beats_per_bar\": %d,\n"
            "  \"bpm\": %.2f,\n"
            "  \"extra_latency_frames\": %d,\n"
            "  \"samples\": [\n",
            impl_->sample_rate, st.master_bar_frames, st.beats_per_bar,
            derive_bpm(st.master_bar_frames, st.beats_per_bar, impl_->sample_rate),
            impl_->extra_latency_frames);
        for (size_t i = 0; i < impl_->samples.size(); ++i) {
            const Sample& s = *impl_->samples[i];
            std::fprintf(f,
                "    {\"id\": %llu, \"bank\": %d, \"file\": \"sample-%llu-bank%d.wav\","
                " \"offset_in_bar\": %u, \"length_frames\": %u, \"volume\": %.3f,"
                " \"speed\": %d, \"reverse\": %s, \"repeat\": %s,"
                " \"start_offset_frames\": %d, \"is_master\": %s}%s\n",
                (unsigned long long)s.id, int(s.bank) + 1,
                (unsigned long long)s.id, int(s.bank) + 1,
                s.recorded_offset_in_bar, s.recorded_length_frames, s.volume,
                int(s.speed), s.reverse ? "true" : "false", s.repeat ? "true" : "false",
                s.start_offset_frames, s.is_master_loop ? "true" : "false",
                (i + 1 < impl_->samples.size()) ? "," : "");
        }
        std::fprintf(f, "  ]\n}\n");
        std::fclose(f);
        ++written;
    }
    std::fprintf(stderr, "[engine] exported %d file(s) to %s\n", written, dir);
    return written;
}

} // namespace tl
