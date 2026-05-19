#include "audio_engine.h"
#include "command_queue.h"
#include "rolling_buffer.h"
#include "sample.h"
#include "bpm.h"
#include "mixer.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tl {

struct AudioEngine::Impl {
    StatePublisher publisher;
    EngineState state;        // RT-thread-owned working copy
    CommandQueue<256> commands;
    RollingBuffer rolling{kFallbackBufferFrames, kChannels};
    std::vector<std::unique_ptr<Sample>> samples;
    std::unique_ptr<Sample> active_recording;
    SampleId next_id{1};
    int sample_rate{kSampleRate};
    Frames global_play_head{0};   // wraps modulo master_bar_frames once set
    int round_trip_latency_frames{0};

    Impl() { init_default_state(state); publisher.publish(state); }

    void apply(const Command& c) {
        switch (c.type) {
        case CommandType::StartRecording: {
            if (active_recording) return; // already recording, ignore
            active_recording = std::make_unique<Sample>(
                next_id++, c.bank, kMaxRecFrames, kChannels);
            active_recording->position_x = c.fx;
            active_recording->position_y = c.fy;
            active_recording->recording = true;
            // recorded_offset_in_bar captured at recording start, with latency compensation
            if (state.master_bar_frames > 0) {
                // Compensate for round-trip latency: the audio the user hears now
                // was emitted ~output_latency ago, and their played notes arrive at
                // the engine ~input_latency late. Shifting the offset backward by
                // round_trip_latency makes the playback land in sync with the loop.
                Frames latency = Frames(round_trip_latency_frames);
                Frames L = state.master_bar_frames;
                Frames adjusted = (global_play_head + L - (latency % L)) % L;
                active_recording->recorded_offset_in_bar = adjusted;
            }
            state.recording = true;
            state.recording_sample_id = active_recording->id;
            break;
        }
        case CommandType::StopRecording: {
            if (!active_recording) return;
            active_recording->recording = false;

            // ── Trim leading silence ────────────────────────────────────────
            // Find the first frame with amplitude above threshold across any channel.
            constexpr float kSilenceThreshold = 0.02f;  // ~-34 dB
            Sample& rec = *active_recording;
            Frames first_audible = 0;
            bool found_audible = false;
            for (Frames f = 0; f < rec.recorded_length_frames; ++f) {
                bool audible = false;
                for (int ch = 0; ch < rec.channels; ++ch) {
                    if (std::abs(rec.pcm[f * rec.channels + ch]) > kSilenceThreshold) {
                        audible = true; break;
                    }
                }
                if (audible) { first_audible = f; found_audible = true; break; }
            }
            if (found_audible && first_audible > 0 && first_audible < rec.recorded_length_frames) {
                // Shift PCM backward by first_audible frames.
                Frames new_len = rec.recorded_length_frames - first_audible;
                std::memmove(rec.pcm.data(),
                             rec.pcm.data() + first_audible * rec.channels,
                             new_len * rec.channels * sizeof(float));
                rec.recorded_length_frames = new_len;
                // Also shift the recorded_offset_in_bar forward by the trimmed amount.
                // (The recording effectively started later than the user clicked.)
                if (state.master_bar_frames > 0) {
                    rec.recorded_offset_in_bar =
                        (rec.recorded_offset_in_bar + first_audible) % state.master_bar_frames;
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
            if (state.master_bar_frames == 0) {
                state.master_bar_frames = len;
                rolling = RollingBuffer(state.master_bar_frames, kChannels);
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
        case CommandType::SetBankMode:    state.banks[c.bank].mode = c.ivalue == 1 ? BankMode::OneShot : BankMode::Loop; break;

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
            Frames len = state.master_bar_frames;
            auto sample = std::make_unique<Sample>(next_id++, c.bank, kMaxRecFrames, kChannels);
            sample->position_x = 0.5f; sample->position_y = 0.5f;
            sample->recorded_offset_in_bar = global_play_head;
            sample->recording = true;
            std::vector<float> buf(size_t(len) * kChannels);
            rolling.read_last(buf.data(), len);
            sample->append(buf.data(), len);
            sample->recording = false;
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
        default: break; // other commands implemented in later tasks
        }
    }

    void publish_state() {
        // Compute 'playing' indicator for each sample before mirroring to state
        for (auto& up : samples) {
            BankMode m = state.banks[up->bank].mode;
            if (m == BankMode::Loop) {
                up->playing = up->enabled && !state.banks[up->bank].muted;
            } else {
                // up->playing is already managed by mixer (set to false when one_shot finishes)
            }
        }

        // mirror samples vector into state.samples
        int n = std::min(int(samples.size()), kMaxSamples);
        for (int i = 0; i < n; ++i) {
            const Sample& s = *samples[i];
            SampleState& ss = state.samples[i];
            ss.id = s.id; ss.bank = s.bank;
            ss.volume = s.volume; ss.reverse = s.reverse; ss.speed = s.speed;
            ss.enabled = s.enabled; ss.playing = s.playing; ss.recording = s.recording;
            ss.position_x = s.position_x; ss.position_y = s.position_y;
            ss.recorded_length_frames = s.recorded_length_frames;
            ss.recorded_offset_in_bar = s.recorded_offset_in_bar;
        }
        // Also mirror the active recording (if any) so the UI shows the box during recording
        if (active_recording && n < kMaxSamples) {
            const Sample& s = *active_recording;
            SampleState& ss = state.samples[n];
            ss.id = s.id; ss.bank = s.bank;
            ss.volume = s.volume; ss.reverse = s.reverse; ss.speed = s.speed;
            ss.enabled = s.enabled; ss.playing = s.playing; ss.recording = s.recording;
            ss.position_x = s.position_x; ss.position_y = s.position_y;
            ss.recorded_length_frames = s.recorded_length_frames;
            ss.recorded_offset_in_bar = s.recorded_offset_in_bar;
            n++;
        }
        state.sample_count = n;
        state.bpm = float(derive_bpm(state.master_bar_frames, state.beats_per_bar, sample_rate));
        state.version++;
        publisher.publish(state);
    }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() = default;

void AudioEngine::process(const float* in, float* out, int frame_count) {
    // 1. Drain commands
    Command c{};
    while (impl_->commands.pop(c)) impl_->apply(c);

    // 2. Append input to rolling buffer
    impl_->rolling.write(in, Frames(frame_count));

    // 3. Append input to active recording if any
    if (impl_->active_recording) {
        impl_->active_recording->append(in, Frames(frame_count));
        impl_->state.recording_duration_frames = impl_->active_recording->recorded_length_frames;
    }

    // 4. Zero output then mix (using current play_head)
    std::memset(out, 0, sizeof(float) * size_t(frame_count) * kChannels);
    if (impl_->state.master_playing) {
        std::vector<Sample*> ptrs;
        ptrs.reserve(impl_->samples.size());
        for (auto& up : impl_->samples) ptrs.push_back(up.get());
        mix(ptrs, impl_->state.banks, impl_->state.master_bar_frames,
            impl_->global_play_head, frame_count, out);
    }

    // 5. Advance global play head (AFTER mix)
    if (impl_->state.master_bar_frames > 0) {
        impl_->global_play_head =
            (impl_->global_play_head + Frames(frame_count)) % impl_->state.master_bar_frames;
    }

    // 6. Publish state
    impl_->publish_state();
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

} // namespace tl
