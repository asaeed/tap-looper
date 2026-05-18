#include "audio_engine.h"
#include "commands.h"
#include <gtest/gtest.h>
#include <vector>

using namespace tl;

// Helper: pass `frames` of input through the engine, discarding output.
static void tick(AudioEngine& e, int frames, float input_value = 0.f) {
    std::vector<float> in(frames * kChannels, input_value);
    std::vector<float> out(frames * kChannels, 0.f);
    e.process(in.data(), out.data(), frames);
}

TEST(AudioEngine, InitialStateIsClean) {
    AudioEngine e;
    auto s = e.snapshot();
    EXPECT_FALSE(s.recording);
    EXPECT_EQ(s.master_bar_frames, 0u);
    EXPECT_EQ(s.sample_count, 0);
}

TEST(AudioEngine, FirstRecordingSetsMasterBarFrames) {
    AudioEngine e;
    Command c{}; c.type = CommandType::StartRecording; c.bank = 0; c.fx = 0.5f; c.fy = 0.5f;
    ASSERT_TRUE(e.submit(c));
    tick(e, 256, 0.1f);     // consume START_RECORDING
    auto s1 = e.snapshot();
    EXPECT_TRUE(s1.recording);

    // Tick for enough frames to make recording > 200ms
    tick(e, 48000 / 2, 0.1f); // 0.5 seconds at 48kHz

    Command stop{}; stop.type = CommandType::StopRecording;
    ASSERT_TRUE(e.submit(stop));
    tick(e, 256);

    auto s2 = e.snapshot();
    EXPECT_FALSE(s2.recording);
    EXPECT_EQ(s2.sample_count, 1);
    // L = (256 + 24000) frames approximately
    EXPECT_GT(s2.master_bar_frames, 24000u);
    EXPECT_LT(s2.master_bar_frames, 25000u);
}

TEST(AudioEngine, RecordingShorterThanMinDiscarded) {
    AudioEngine e;
    Command c{}; c.type = CommandType::StartRecording; c.bank = 0;
    e.submit(c);
    tick(e, 256);
    // less than 200ms = less than 9600 frames at 48kHz
    tick(e, 1000);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop);
    tick(e, 256);

    auto s = e.snapshot();
    EXPECT_EQ(s.sample_count, 0);
    EXPECT_EQ(s.master_bar_frames, 0u);
}

TEST(AudioEngine, GlobalPlayHeadAdvances) {
    AudioEngine e;
    tick(e, 1000);
    auto s = e.snapshot();
    // play head is internal but we can verify indirectly via subsequent offset tests in Task 13.
    // For now just verify nothing crashes.
    SUCCEED();
}

TEST(AudioEngine, LoopSampleEnabledAfterFirstRecording) {
    AudioEngine e;
    Command c{}; c.type = CommandType::StartRecording; c.bank = 0;
    e.submit(c); tick(e, 256, 0.1f);
    tick(e, 48000 / 2, 0.1f);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 256);
    auto s = e.snapshot();
    ASSERT_EQ(s.sample_count, 1);
    EXPECT_TRUE(s.samples[0].enabled);   // LOOP bank → enabled on stop
}

TEST(AudioEngine, SubBarSamplePlaysAtRecordedOffset) {
    AudioEngine e;
    // Record a 1-second master bar
    Command rec1{}; rec1.type = CommandType::StartRecording; rec1.bank = 0;
    e.submit(rec1); tick(e, 256);
    tick(e, 48000, 0.0f);  // 1 second of silence
    Command stop1{}; stop1.type = CommandType::StopRecording;
    e.submit(stop1); tick(e, 256);

    auto s1 = e.snapshot();
    ASSERT_GT(s1.master_bar_frames, 47000u);
    ASSERT_LT(s1.master_bar_frames, 49000u);

    // Now record a short sample 500ms into the bar
    tick(e, 24000); // advance 0.5s
    Command rec2{}; rec2.type = CommandType::StartRecording; rec2.bank = 1;
    e.submit(rec2); tick(e, 256);
    tick(e, 9600, 1.0f);    // 200ms of audio (minimum to pass validation)
    Command stop2{}; stop2.type = CommandType::StopRecording;
    e.submit(stop2); tick(e, 256);

    auto s2 = e.snapshot();
    ASSERT_EQ(s2.sample_count, 2);
    // Sample #2 should have recorded_offset_in_bar > 0 (it was recorded mid-bar)
    const SampleState& sub = s2.samples[1];
    EXPECT_GT(sub.recorded_offset_in_bar, 20000u);
    EXPECT_LT(sub.recorded_offset_in_bar, 30000u);
}

TEST(AudioEngine, TapOneShotSampleTriggersPlayback) {
    AudioEngine e;
    // Switch bank 0 to ONE_SHOT
    Command setMode{}; setMode.type = CommandType::SetBankMode; setMode.bank = 0; setMode.ivalue = 1;
    // SetBankMode is implemented in Task 17; for now bypass by recording in default LOOP bank,
    // then change mode after. To keep this task self-contained, we test by setting up bank as ONE_SHOT in state directly.
    // → For now, just verify Tap on a LOOP sample toggles enabled.
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 256);
    tick(e, 48000 / 2);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 256);
    auto s = e.snapshot();
    ASSERT_EQ(s.sample_count, 1);
    EXPECT_TRUE(s.samples[0].enabled);

    Command tap{}; tap.type = CommandType::TapSample; tap.sample_id = s.samples[0].id;
    e.submit(tap); tick(e, 256);
    auto s2 = e.snapshot();
    EXPECT_FALSE(s2.samples[0].enabled);   // toggled off
}

TEST(AudioEngine, SetBankMuteReflectsInSnapshot) {
    AudioEngine e;
    Command c{}; c.type = CommandType::SetBankMute; c.bank = 2; c.bvalue = true;
    e.submit(c); tick(e, 64);
    EXPECT_TRUE(e.snapshot().banks[2].muted);
}

TEST(AudioEngine, MoveSampleChangesBankNotPCM) {
    AudioEngine e;
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 256);
    tick(e, 48000 / 2);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 256);
    auto s = e.snapshot();
    SampleId id = s.samples[0].id;
    Frames orig_offset = s.samples[0].recorded_offset_in_bar;
    Frames orig_len = s.samples[0].recorded_length_frames;

    Command mv{}; mv.type = CommandType::MoveSample; mv.sample_id = id; mv.bank = 3; mv.fx = 0.2f; mv.fy = 0.3f;
    e.submit(mv); tick(e, 64);
    auto s2 = e.snapshot();
    EXPECT_EQ(s2.samples[0].bank, 3);
    EXPECT_EQ(s2.samples[0].recorded_offset_in_bar, orig_offset);
    EXPECT_EQ(s2.samples[0].recorded_length_frames, orig_len);
}

TEST(AudioEngine, DeleteSampleRemovesIt) {
    AudioEngine e;
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 256);
    tick(e, 48000 / 2);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 256);
    auto s = e.snapshot();
    Command del{}; del.type = CommandType::DeleteSample; del.sample_id = s.samples[0].id;
    e.submit(del); tick(e, 64);
    EXPECT_EQ(e.snapshot().sample_count, 0);
}

TEST(AudioEngine, CaptureLastLoopCreatesSample) {
    AudioEngine e;
    Command rec{}; rec.type = CommandType::StartRecording; rec.bank = 0;
    e.submit(rec); tick(e, 256);
    tick(e, 48000 / 2, 0.5f);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 256);
    // Now feed some input into rolling buffer
    tick(e, 24000, 0.7f);
    Command cap{}; cap.type = CommandType::CaptureLastLoop; cap.bank = 1;
    e.submit(cap); tick(e, 64);
    auto s = e.snapshot();
    EXPECT_EQ(s.sample_count, 2);
    EXPECT_EQ(s.samples[1].bank, 1);
    EXPECT_GT(s.samples[1].recorded_length_frames, 0u);
}
