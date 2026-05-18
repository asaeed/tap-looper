#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio_io.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace tl {

class MiniaudioIO : public AudioIO {
public:
    bool start(AudioEngine& engine) override {
        engine_ = &engine;

        ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
        cfg.sampleRate         = kSampleRate;
        cfg.capture.format     = ma_format_f32;
        cfg.capture.channels   = kChannels;
        cfg.playback.format    = ma_format_f32;
        cfg.playback.channels  = kChannels;
        cfg.periodSizeInFrames = 256;       // ~5ms at 48kHz; tweak after measurement
        cfg.periods            = 2;
        cfg.dataCallback       = &OnAudio;
        cfg.pUserData          = this;

        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) {
            std::fprintf(stderr, "miniaudio: device init failed\n");
            return false;
        }
        if (ma_device_start(&device_) != MA_SUCCESS) {
            std::fprintf(stderr, "miniaudio: device start failed\n");
            ma_device_uninit(&device_);
            return false;
        }
        started_ = true;
        return true;
    }

    void stop() override {
        if (started_) {
            ma_device_uninit(&device_);
            started_ = false;
        }
    }

private:
    static void OnAudio(ma_device* device, void* output, const void* input, ma_uint32 frames) {
        auto* self = static_cast<MiniaudioIO*>(device->pUserData);
        const float* in  = static_cast<const float*>(input);
        float*       out = static_cast<float*>(output);
        self->engine_->process(in, out, int(frames));
    }

    AudioEngine* engine_ = nullptr;
    ma_device device_{};
    bool started_ = false;
};

extern "C" AudioIO* tl_create_audio_io() {
    return new MiniaudioIO();
}

} // namespace tl
