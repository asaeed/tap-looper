#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio_io.h"

#include <cstdio>
#include <memory>
#include <vector>

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#endif

namespace tl {

#ifdef __APPLE__
// CoreAudio device latency + safety offset (frames) for one direction.
// miniaudio's period sizes only cover the ring buffers; the ADC/DAC and driver
// add several ms that must be part of the round-trip compensation.
static int coreaudio_device_latency_frames(ma_uint32 device_object_id, bool input) {
    if (device_object_id == 0) return 0;
    AudioObjectID dev = AudioObjectID(device_object_id);
    AudioObjectPropertyScope scope =
        input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
    UInt32 latency = 0, safety = 0;
    UInt32 size = sizeof(UInt32);
    AudioObjectPropertyAddress addr{
        kAudioDevicePropertyLatency, scope, kAudioObjectPropertyElementMain};
    AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &latency);
    addr.mSelector = kAudioDevicePropertySafetyOffset;
    size = sizeof(UInt32);
    AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &safety);
    return int(latency + safety);
}
#endif

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
        cfg.periodSizeInFrames = 128;                           // ~2.7ms at 48kHz; was 256
        cfg.periods            = 2;
        cfg.performanceProfile = ma_performance_profile_low_latency;   // NEW
        cfg.dataCallback       = &OnAudio;
        cfg.pUserData          = this;

        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) {
            std::fprintf(stderr, "miniaudio: device init failed\n");
            return false;
        }
        std::fprintf(stderr,
            "[miniaudio] sample_rate=%u, playback_period=%u, capture_period=%u, internal_periods=%u\n",
            (unsigned)device_.sampleRate,
            (unsigned)device_.playback.internalPeriodSizeInFrames,
            (unsigned)device_.capture.internalPeriodSizeInFrames,
            (unsigned)device_.playback.internalPeriods);
        if (ma_device_start(&device_) != MA_SUCCESS) {
            std::fprintf(stderr, "miniaudio: device start failed\n");
            ma_device_uninit(&device_);
            return false;
        }
        started_ = true;

        // Compute round-trip latency in frames and report it to the engine
        int input_frames  = int(device_.capture.internalPeriodSizeInFrames  * device_.capture.internalPeriods);
        int output_frames = int(device_.playback.internalPeriodSizeInFrames * device_.playback.internalPeriods);
        int device_extra = 0;
#ifdef __APPLE__
        device_extra = coreaudio_device_latency_frames(device_.coreaudio.deviceObjectIDCapture, true)
                     + coreaudio_device_latency_frames(device_.coreaudio.deviceObjectIDPlayback, false);
#endif
        int round_trip = input_frames + output_frames + device_extra;
        std::fprintf(stderr, "[miniaudio] round_trip_latency_frames=%d (input=%d, output=%d, device=%d)\n",
                     round_trip, input_frames, output_frames, device_extra);
        engine.set_round_trip_latency_frames(round_trip);

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
