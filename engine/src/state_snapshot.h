#pragma once
#include "types.h"
#include <memory>
#include <mutex>

namespace tl {
enum class BankMode : uint8_t { Loop = 0, OneShot = 1 };
enum class Speed   : uint8_t { Half = 0, Normal = 1, Double = 2 };

struct SampleState {
    SampleId id;
    BankId bank;
    float volume;
    bool reverse;
    Speed speed;
    bool enabled;
    bool playing;
    bool recording;
    float position_x, position_y;
    Frames recorded_length_frames;
    Frames recorded_offset_in_bar;
};

struct BankState {
    BankId id;
    BankMode mode;
    float volume;
    bool muted;
};

struct EngineState {
    BankState banks[kNumBanks];
    SampleState samples[kMaxSamples];
    int sample_count;
    bool recording;
    Frames recording_duration_frames;
    SampleId recording_sample_id;
    BankId focused_bank;
    Frames master_bar_frames;
    int beats_per_bar;
    float bpm;
    bool master_playing;
    uint64_t version;
};

// Default-initialize a state to "fresh session" values.
void init_default_state(EngineState& s);

// Thread-safe state publisher: writer thread calls publish(); readers call snapshot().
// Internally uses a mutex-protected shared_ptr to swap whole state objects.
class StatePublisher {
public:
    StatePublisher();
    void publish(const EngineState& s);
    EngineState snapshot() const;
private:
    mutable std::mutex mutex_;
    std::shared_ptr<EngineState> current_;
};

} // namespace tl
