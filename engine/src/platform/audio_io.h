#pragma once
#include "../audio_engine.h"

namespace tl {

// Platform-specific audio I/O. Owns the platform stream(s) and drives engine.process().
class AudioIO {
public:
    virtual ~AudioIO() = default;
    virtual bool start(AudioEngine& engine) = 0;
    virtual void stop() = 0;
};

} // namespace tl
