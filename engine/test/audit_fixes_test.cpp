// Tests for the 2026-07 audit fixes: focused bank, cancel/auto-stop recording,
// sample cap, solo-aware playing indicator, offset normalization, speed-scaled
// play windows, envelope, input peak, metronome, and WAV export.
#include "audio_engine.h"
#include "commands.h"
#include "mixer.h"
#include "sample.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>

using namespace tl;

namespace {

void tick(AudioEngine& e, int frames, float input_value = 0.f) {
    std::vector<float> in(size_t(frames) * kChannels, input_value);
    std::vector<float> out(size_t(frames) * kChannels, 0.f);
    e.process(in.data(), out.data(), frames);
}

void submit(AudioEngine& e, CommandType t,
            SampleId id = 0, int bank = 0, int ivalue = 0, bool bvalue = false) {
    Command c{};
    c.type = t; c.sample_id = id; c.bank = BankId(bank);
    c.ivalue = ivalue; c.bvalue = bvalue;
    ASSERT_TRUE(e.submit(c));
}

// Record one ~500ms sample of constant amplitude into `bank`.
void record_sample(AudioEngine& e, int bank, float amp = 0.5f) {
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = BankId(bank);
    ASSERT_TRUE(e.submit(rec));
    tick(e, 256, amp);
    tick(e, 24000, amp);
    Command stop{}; stop.type = CommandType::StopRecording;
    ASSERT_TRUE(e.submit(stop));
    tick(e, 256, amp);
}

} // namespace

TEST(AuditFixes, StartRecordingSetsFocusedBank) {
    AudioEngine e;
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 3;
    e.submit(rec); tick(e, 256, 0.5f);
    EXPECT_EQ(e.snapshot().focused_bank, 3);
}

TEST(AuditFixes, CancelRecordingDiscardsIt) {
    AudioEngine e;
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 256, 0.5f);
    tick(e, 24000, 0.5f);
    submit(e, CommandType::CancelRecording);
    tick(e, 256);
    auto s = e.snapshot();
    EXPECT_FALSE(s.recording);
    EXPECT_EQ(s.sample_count, 0);
    EXPECT_EQ(s.master_bar_frames, 0u);
}

TEST(AuditFixes, RecordingAutoStopsAtCap) {
    AudioEngine e;
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 256, 0.5f);
    tick(e, int(kMaxRecFrames) + 4800, 0.5f);  // exceed the 60s cap
    auto s = e.snapshot();
    EXPECT_FALSE(s.recording);                 // auto-finalized, not frozen
    EXPECT_EQ(s.sample_count, 1);
}

TEST(AuditFixes, SoloMarksOtherBanksNotPlaying) {
    AudioEngine e;
    record_sample(e, 0);
    auto s1 = e.snapshot();
    ASSERT_EQ(s1.sample_count, 1);
    EXPECT_TRUE(s1.samples[0].playing);

    submit(e, CommandType::SetBankSolo, 0, /*bank=*/1, 0, /*bvalue=*/true);
    tick(e, 64);
    EXPECT_FALSE(e.snapshot().samples[0].playing);  // bank 0 silent while bank 1 soloed

    submit(e, CommandType::SetBankSolo, 0, /*bank=*/1, 0, /*bvalue=*/false);
    tick(e, 64);
    EXPECT_TRUE(e.snapshot().samples[0].playing);
}

TEST(AuditFixes, EnvelopeComputedAfterRecording) {
    AudioEngine e;
    record_sample(e, 0, 0.5f);
    auto s = e.snapshot();
    ASSERT_EQ(s.sample_count, 1);
    for (int i = 0; i < kEnvBins; ++i) {
        EXPECT_GT(s.samples[0].env[i], 0.3f) << "bin " << i;
    }
}

TEST(AuditFixes, InputPeakTracksInput) {
    AudioEngine e;
    submit(e, CommandType::SetBeatsPerBar, 0, 0, 4);  // force a publish this tick
    tick(e, 256, 0.6f);
    EXPECT_GE(e.snapshot().input_peak, 0.6f);
}

TEST(AuditFixes, MetronomeClicksWithoutSamples) {
    AudioEngine e;
    submit(e, CommandType::SetMasterBarFrames, 0, 0, 48000);
    submit(e, CommandType::SetMetronome, 0, 0, 0, true);
    std::vector<float> in(256 * kChannels, 0.f);
    std::vector<float> out(256 * kChannels, 0.f);
    e.process(in.data(), out.data(), 256);
    float peak = 0.f;
    for (float v : out) peak = std::max(peak, v < 0 ? -v : v);
    EXPECT_GT(peak, 0.05f);  // click audible at the downbeat
}

TEST(AuditFixes, ExportWritesWavAndJson) {
    AudioEngine e;
    record_sample(e, 0, 0.5f);
    auto s = e.snapshot();
    ASSERT_EQ(s.sample_count, 1);

    std::string dir = testing::TempDir() + "tl_export_test";
    int written = e.export_session(dir.c_str());
    EXPECT_EQ(written, 2);  // 1 wav + session.json

    char wav_path[256];
    std::snprintf(wav_path, sizeof(wav_path), "%s/sample-%llu-bank1.wav",
                  dir.c_str(), (unsigned long long)s.samples[0].id);
    FILE* f = std::fopen(wav_path, "rb");
    ASSERT_NE(f, nullptr);
    unsigned char h[44];
    ASSERT_EQ(std::fread(h, 1, 44, f), 44u);
    std::fclose(f);
    EXPECT_EQ(std::string((char*)h, 4), "RIFF");
    EXPECT_EQ(h[20], 3);  // IEEE float format tag
    uint32_t data_bytes = uint32_t(h[40]) | (uint32_t(h[41]) << 8)
                        | (uint32_t(h[42]) << 16) | (uint32_t(h[43]) << 24);
    EXPECT_EQ(data_bytes, uint32_t(s.samples[0].recorded_length_frames) * kChannels * 4);

    FILE* j = std::fopen((dir + "/session.json").c_str(), "r");
    ASSERT_NE(j, nullptr);
    std::fclose(j);
}

// A multi-bar sample must play its LATER bars across callbacks — the engine
// used to wrap the play head mod bar_frames, so multi-bar samples repeated
// their first bar forever.
TEST(AuditFixes, MultiBarSamplePlaysPastFirstBar) {
    AudioEngine e;
    submit(e, CommandType::SetExtraLatencyFrames, 0, 0, /*ivalue=*/0);  // deterministic play head

    // Master: 0.5s at amplitude 0.2 → L = 24000
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 24000, 0.2f);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 16);
    ASSERT_EQ(e.snapshot().master_bar_frames, 24000u);

    // 2-bar sample: first bar at 0.4, second bar at 0.8
    Command rec2{}; rec2.type = CommandType::StartRecording; rec2.bank = 1;
    e.submit(rec2); tick(e, 24000, 0.4f); tick(e, 24000, 0.8f);
    Command stop2{}; stop2.type = CommandType::StopRecording;
    e.submit(stop2); tick(e, 16);
    ASSERT_EQ(e.snapshot().sample_count, 2);

    // Align the play head to a 2-bar boundary, then capture two consecutive bars.
    tick(e, 47968);  // play head is at 48032 here; 48032 + 47968 ≡ 0 (mod 48000)
    std::vector<float> in(24000 * kChannels, 0.f);
    std::vector<float> out1(24000 * kChannels, 0.f);
    std::vector<float> out2(24000 * kChannels, 0.f);
    e.process(in.data(), out1.data(), 24000);
    e.process(in.data(), out2.data(), 24000);

    auto mean_abs = [](const std::vector<float>& v) {
        double s = 0; for (float x : v) s += (x < 0 ? -x : x); return s / v.size();
    };
    double m1 = mean_abs(out1), m2 = mean_abs(out2);
    EXPECT_GT(m1, 0.4);   // master (0.2) + sample bar 1 (0.4)
    EXPECT_LT(m1, 0.75);
    EXPECT_GT(m2, m1 + 0.15);   // bar 2 (0.8) actually plays — not bar 1 again
}

// Loop takes longer than a bar must snap to the nearest whole bar (pad or
// truncate) so playback wraps cleanly at the seam.
TEST(AuditFixes, LoopLengthQuantizedToBarGrid) {
    AudioEngine e;
    submit(e, CommandType::SetExtraLatencyFrames, 0, 0, /*ivalue=*/0);

    // Master: L = 24000
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 24000, 0.2f);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 16);
    ASSERT_EQ(e.snapshot().master_bar_frames, 24000u);

    // ~1.7-bar take → rounds UP to 2 bars (padded with silence)
    Command rec2{}; rec2.type = CommandType::StartRecording; rec2.bank = 1;
    e.submit(rec2); tick(e, 40800, 0.5f);
    e.submit(stop); tick(e, 16);
    EXPECT_EQ(e.snapshot().samples[1].recorded_length_frames, 48000u);

    // ~2.2-bar take → rounds DOWN to 2 bars (truncated)
    Command rec3{}; rec3.type = CommandType::StartRecording; rec3.bank = 2;
    e.submit(rec3); tick(e, 52800, 0.5f);
    e.submit(stop); tick(e, 16);
    EXPECT_EQ(e.snapshot().samples[2].recorded_length_frames, 48000u);

    // Sub-bar take stays untouched
    Command rec4{}; rec4.type = CommandType::StartRecording; rec4.bank = 3;
    e.submit(rec4); tick(e, 12000, 0.5f);
    e.submit(stop); tick(e, 16);
    EXPECT_EQ(e.snapshot().samples[3].recorded_length_frames, 12000u);
}

// A multi-bar overdub started mid-bar must play back at the grid position it
// was performed at — anchoring at bar 0 rotated it by its start position.
TEST(AuditFixes, MultiBarOverdubAnchorsAtRecordingStart) {
    AudioEngine e;
    submit(e, CommandType::SetExtraLatencyFrames, 0, 0, /*ivalue=*/0);

    // Master: L = 24000 at amplitude 0.2; play head = 0 at stop, 16 after tick.
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 24000, 0.2f);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 16);
    ASSERT_EQ(e.snapshot().master_bar_frames, 24000u);

    // Start a 2-bar overdub mid-bar at timeline position 18000:
    // first bar of content 0.5, second bar 0.9.
    tick(e, 17984);   // play head: 16 → 18000
    Command rec2{}; rec2.type = CommandType::StartRecording; rec2.bank = 1;
    e.submit(rec2); tick(e, 24000, 0.5f); tick(e, 24000, 0.9f);
    e.submit(stop); tick(e, 16);
    ASSERT_EQ(e.snapshot().sample_count, 2);
    // play head now 66016 ≡ 18016 (mod 48000); loop anchor = 18000.

    tick(e, 11984);   // → 78000 ≡ 30000 (mod 48000)
    std::vector<float> in(12000 * kChannels, 0.f);
    std::vector<float> out1(12000 * kChannels, 0.f);
    e.process(in.data(), out1.data(), 12000);   // local 12000..23999 → content 0.5
    std::vector<float> out2(6000 * kChannels, 0.f);
    e.process(in.data(), out2.data(), 6000);    // local 24000..29999 → content 0.9

    auto mean_abs = [](const std::vector<float>& v) {
        double s = 0; for (float x : v) s += (x < 0 ? -x : x); return s / v.size();
    };
    double m1 = mean_abs(out1), m2 = mean_abs(out2);
    EXPECT_GT(m1, 0.5);    // master 0.2 + first-bar content 0.5
    EXPECT_LT(m1, 0.85);   // bar-0 anchoring would put 0.9 content here (~1.1)
    EXPECT_GT(m2, 0.9);    // master 0.2 + second-bar content 0.9
}

// ── Mixer-level checks ─────────────────────────────────────────────────────

namespace {
Sample make_ramp_sample(Frames len) {
    Sample s(1, 0, len, 2);
    s.recording = true;
    std::vector<float> pcm(size_t(len) * 2);
    for (Frames f = 0; f < len; ++f) { pcm[f*2] = float(f + 1); pcm[f*2+1] = float(f + 1); }
    s.append(pcm.data(), len);
    s.recording = false;
    s.enabled = true;
    return s;
}
} // namespace

TEST(AuditFixes, DoubleSpeedSubBarWindowIsHalf) {
    Sample s = make_ramp_sample(4);   // pcm 1,2,3,4
    s.speed = Speed::Double;
    BankState banks[kNumBanks] = {};
    for (int i = 0; i < kNumBanks; ++i) { banks[i].id = BankId(i); banks[i].mode = BankMode::Loop; banks[i].volume = 1.f; }
    std::vector<Sample*> samples = {&s};
    std::vector<float> out(8 * 2, 0.f);
    mix(samples, banks, /*bar*/8, /*play_head*/0, /*frames*/8, out.data(), 0);
    EXPECT_FLOAT_EQ(out[0], 1.0f);   // frame 0 → pcm[0]
    EXPECT_FLOAT_EQ(out[2], 3.0f);   // frame 1 → pcm[2]
    EXPECT_FLOAT_EQ(out[4], 0.0f);   // window over after play_len=2 frames
    EXPECT_FLOAT_EQ(out[14], 0.0f);
}

TEST(AuditFixes, OffsetBeyondBarIsNormalized) {
    Sample s = make_ramp_sample(4);   // pcm 1,2,3,4
    s.recorded_offset_in_bar = 10;    // bar later shrunk to 4 → normalizes to 2
    BankState banks[kNumBanks] = {};
    for (int i = 0; i < kNumBanks; ++i) { banks[i].id = BankId(i); banks[i].mode = BankMode::Loop; banks[i].volume = 1.f; }
    std::vector<Sample*> samples = {&s};
    std::vector<float> out(4 * 2, 0.f);
    mix(samples, banks, /*bar*/4, /*play_head*/0, /*frames*/4, out.data(), /*extra_latency*/0);
    // offset 10 % 4 = 2: window wraps; bar_pos 0 → local 2 → pcm[2]=3, bar_pos 2 → local 0 → pcm[0]=1
    EXPECT_FLOAT_EQ(out[0], 3.0f);
    EXPECT_FLOAT_EQ(out[4], 1.0f);
}
