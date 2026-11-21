#include "WavIO.h"
#include "../dsp/Random.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace pad {

namespace {
void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); }
void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff); }
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}

bool writeWav24(const std::string& path, const std::vector<float>& x, int sampleRate) {
    const uint32_t frames = static_cast<uint32_t>(x.size() / 2);
    const uint32_t dataBytes = frames * 2 * 3;
    std::vector<uint8_t> b;
    b.reserve(44 + dataBytes);
    b.insert(b.end(), {'R', 'I', 'F', 'F'}); put32(b, 36 + dataBytes);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); put32(b, 16);
    put16(b, 1); put16(b, 2); put32(b, static_cast<uint32_t>(sampleRate));
    put32(b, static_cast<uint32_t>(sampleRate) * 6); put16(b, 6); put16(b, 24);
    b.insert(b.end(), {'d', 'a', 't', 'a'}); put32(b, dataBytes);
    Rng rng(12345);
    const float lsb = 1.0f / 8388608.0f;
    for (float v : x) {
        const float d = (rng.uniform() - rng.uniform()) * lsb; // TPDF
        float s = std::fmax(-1.0f, std::fmin(1.0f, v + d));
        int32_t q = static_cast<int32_t>(std::lrint(s * 8388607.0f));
        b.push_back(q & 0xff); b.push_back((q >> 8) & 0xff); b.push_back((q >> 16) & 0xff);
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    std::fclose(f);
    return ok;
}

bool readWav(const std::string& path, WavData& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> b;
    uint8_t tmp[65536];
    size_t r;
    while ((r = std::fread(tmp, 1, sizeof(tmp), f)) > 0) b.insert(b.end(), tmp, tmp + r);
    std::fclose(f);
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;

    int fmt = 0, ch = 0, bits = 0, rate = 0;
    const uint8_t* data = nullptr;
    uint32_t dataLen = 0;
    size_t p = 12;
    while (p + 8 <= b.size()) {
        const uint32_t len = get32(&b[p + 4]);
        const uint8_t* body = &b[p + 8];
        if (p + 8 + len > b.size()) break;
        if (std::memcmp(&b[p], "fmt ", 4) == 0 && len >= 16) {
            fmt = get16(body); ch = get16(body + 2); rate = static_cast<int>(get32(body + 4)); bits = get16(body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = get16(body + 24); // WAVE_FORMAT_EXTENSIBLE
        } else if (std::memcmp(&b[p], "data", 4) == 0) {
            data = body; dataLen = len;
        }
        p += 8 + len + (len & 1);
    }
    if (!data || ch <= 0 || rate <= 0) return false;
    const int bps = bits / 8;
    if (bps <= 0) return false;
    const size_t n = dataLen / static_cast<size_t>(bps);
    out.sampleRate = rate;
    out.channels = ch;
    out.samples.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* s = data + i * static_cast<size_t>(bps);
        float v = 0.0f;
        if (fmt == 3 && bits == 32) { std::memcpy(&v, s, 4); }
        else if (fmt == 1 && bits == 16) { v = static_cast<int16_t>(get16(s)) / 32768.0f; }
        else if (fmt == 1 && bits == 24) { int32_t q = (s[0] | (s[1] << 8) | (s[2] << 16)); if (q & 0x800000) q |= ~0xFFFFFF; v = q / 8388608.0f; }
        else if (fmt == 1 && bits == 32) { v = static_cast<int32_t>(get32(s)) / 2147483648.0f; }
        else return false;
        out.samples[i] = v;
    }
    return true;
}

std::vector<float> toMono(const WavData& w) {
    std::vector<float> m(w.samples.size() / static_cast<size_t>(std::max(w.channels, 1)));
    for (size_t i = 0; i < m.size(); ++i) {
        float s = 0.0f;
        for (int c = 0; c < w.channels; ++c) s += w.samples[i * static_cast<size_t>(w.channels) + static_cast<size_t>(c)];
        m[i] = s / static_cast<float>(w.channels);
    }
    return m;
}

} // namespace pad
