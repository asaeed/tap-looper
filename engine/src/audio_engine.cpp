#include "audio_engine.h"
#include "command_queue.h"
#include "rolling_buffer.h"
#include "sample.h"
#include "bpm.h"
#include "mixer.h"
#include <memory>
#include <vector>
#include <algorithm>
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
            // recorded_offset_in_bar captured at recording start
            if (state.master_bar_frames > 0) {
                // current play position mod L
                active_recording->recorded_offset_in_bar = global_play_head;
            }
            state.recording = true;
            state.recording_sample_id = active_recording->id;
            break;
        }
        case CommandType::StopRecording: {
            if (!active_recording) return;
            active_recording->recording = false;
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
    {
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

} // namespace tl
