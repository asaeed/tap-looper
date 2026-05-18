#include "bpm.h"

namespace tl {
double derive_bpm(Frames bar_frames, int beats_per_bar, int sample_rate) {
    if (bar_frames == 0 || sample_rate <= 0) return 0.0;
    double bar_seconds = double(bar_frames) / double(sample_rate);
    return double(beats_per_bar) * 60.0 / bar_seconds;
}
} // namespace tl
