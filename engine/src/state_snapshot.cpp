#include "state_snapshot.h"
#include <cstring>

namespace tl {

void init_default_state(EngineState& s) {
    std::memset(&s, 0, sizeof(s));
    s.beats_per_bar = 4;
    s.master_playing = true;
    s.extra_latency_frames = 0;  // by-ear LAT knob; round_trip already compensates
    for (int i = 0; i < kNumBanks; ++i) {
        s.banks[i].id = BankId(i);
        s.banks[i].mode = BankMode::Loop;
        s.banks[i].volume = 1.0f;
        s.banks[i].muted = false;
        s.banks[i].soloed = false;
    }
}

StatePublisher::StatePublisher() {
    auto empty = std::make_shared<EngineState>();
    init_default_state(*empty);
    current_ = empty;
}

void StatePublisher::publish(const EngineState& s) {
    auto copy = std::make_shared<EngineState>(s);
    std::lock_guard<std::mutex> lock(mutex_);
    current_ = copy;
}

EngineState StatePublisher::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return *current_;
}

} // namespace tl
