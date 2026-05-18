#include <jni.h>
#include <memory>
#include "audio_engine.h"
#include "audio_io.h"
#include "commands.h"

using namespace tl;

extern "C" AudioIO* tl_create_audio_io();

struct EngineHandle {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    std::unique_ptr<AudioIO> io { tl_create_audio_io() };
};

#define JF(retval, name) extern "C" JNIEXPORT retval JNICALL Java_com_taplooper_bridge_EngineBridge_##name

JF(jlong, nativeCreate)(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new EngineHandle());
}
JF(void, nativeStart)(JNIEnv*, jobject, jlong h) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    eh->io->start(*eh->engine);
}
JF(void, nativeStop)(JNIEnv*, jobject, jlong h) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    eh->io->stop();
}
JF(void, nativeDestroy)(JNIEnv*, jobject, jlong h) {
    delete reinterpret_cast<EngineHandle*>(h);
}
JF(jboolean, nativeSubmit)(JNIEnv*, jobject, jlong h,
                           jint type, jlong sample_id, jint bank,
                           jfloat fx, jfloat fy, jfloat fvalue,
                           jint ivalue, jboolean bvalue) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    Command c{};
    c.type = CommandType(type);
    c.sample_id = SampleId(sample_id);
    c.bank = BankId(bank);
    c.fx = fx; c.fy = fy; c.fvalue = fvalue;
    c.ivalue = ivalue; c.bvalue = bvalue;
    return eh->engine->submit(c) ? JNI_TRUE : JNI_FALSE;
}

JF(void, nativeSnapshot)(JNIEnv* env, jobject, jlong h,
                         jlongArray longs_out, jfloatArray floats_out) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    EngineState st = eh->engine->snapshot();

    jsize need_longs  = 8 + kNumBanks * 2 + kMaxSamples * 8;
    jsize need_floats = 1 + kNumBanks + kMaxSamples * 4;
    if (env->GetArrayLength(longs_out) < need_longs ||
        env->GetArrayLength(floats_out) < need_floats) return;

    jlong* L = env->GetLongArrayElements(longs_out, nullptr);
    jfloat* F = env->GetFloatArrayElements(floats_out, nullptr);

    L[0] = jlong(st.version);
    L[1] = st.sample_count;
    L[2] = st.focused_bank;
    L[3] = st.master_bar_frames;
    L[4] = st.recording ? 1 : 0;
    L[5] = st.recording_duration_frames;
    L[6] = jlong(st.recording_sample_id);
    L[7] = st.beats_per_bar;

    F[0] = st.bpm;
    int Lp = 8, Fp = 1;
    for (int i = 0; i < kNumBanks; ++i) {
        L[Lp++] = int(st.banks[i].mode);
        L[Lp++] = st.banks[i].muted ? 1 : 0;
        F[Fp++] = st.banks[i].volume;
    }
    for (int i = 0; i < kMaxSamples; ++i) {
        if (i >= st.sample_count) {
            for (int k = 0; k < 8; ++k) L[Lp++] = 0;
            for (int k = 0; k < 4; ++k) F[Fp++] = 0.f;
            continue;
        }
        const SampleState& s = st.samples[i];
        L[Lp++] = jlong(s.id); L[Lp++] = s.bank;
        L[Lp++] = int(s.speed); L[Lp++] = s.enabled ? 1 : 0;
        L[Lp++] = s.playing ? 1 : 0; L[Lp++] = s.recording ? 1 : 0;
        L[Lp++] = s.recorded_length_frames; L[Lp++] = s.recorded_offset_in_bar;
        F[Fp++] = s.volume; F[Fp++] = s.reverse ? 1.f : 0.f;
        F[Fp++] = s.position_x; F[Fp++] = s.position_y;
    }

    env->ReleaseLongArrayElements(longs_out, L, 0);
    env->ReleaseFloatArrayElements(floats_out, F, 0);
}
