#include "wav_writer.h"
#include <cstdio>
#include <cstring>

namespace tl {
namespace {
void put_u32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }
void put_u16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
} // namespace

bool write_wav_f32(const char* path, const float* interleaved,
                   Frames frames, int channels, int sample_rate) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    uint32_t data_bytes = uint32_t(frames) * uint32_t(channels) * 4u;
    uint8_t h[44];
    std::memcpy(h, "RIFF", 4); put_u32(h + 4, 36 + data_bytes); std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4); put_u32(h + 16, 16);
    put_u16(h + 20, 3);                                        // IEEE float
    put_u16(h + 22, uint16_t(channels));
    put_u32(h + 24, uint32_t(sample_rate));
    put_u32(h + 28, uint32_t(sample_rate) * uint32_t(channels) * 4u); // byte rate
    put_u16(h + 32, uint16_t(channels * 4));                   // block align
    put_u16(h + 34, 32);                                       // bits per sample
    std::memcpy(h + 36, "data", 4); put_u32(h + 40, data_bytes);
    bool ok = std::fwrite(h, 1, 44, f) == 44
           && std::fwrite(interleaved, 1, data_bytes, f) == data_bytes;
    std::fclose(f);
    return ok;
}

} // namespace tl
