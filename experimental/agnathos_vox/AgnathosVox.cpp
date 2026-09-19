#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr int kSampleRate = 44100;
constexpr int kBufferSamples = 4096;
constexpr int kBufferCount = 4;
constexpr int kRingSize = 16384;
constexpr int kFftSize = 2048;
constexpr int kBars = 72;
constexpr int kAnchors = 11;
constexpr UINT_PTR kTimerId = 1;

constexpr std::array<float, kAnchors> kAnchorHz{
    174.0f, 285.0f, 288.0f, 384.0f, 396.0f, 417.0f,
    528.0f, 639.0f, 741.0f, 852.0f, 963.0f
};

constexpr std::array<const wchar_t*, kAnchors> kAnchorNames{
    L"174", L"285", L"288", L"384", L"396", L"417",
    L"528", L"639", L"741", L"852", L"963"
};

struct Color {
    int r = 0, g = 0, b = 0;
};

static Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        static_cast<int>(a.r + (b.r - a.r) * t),
        static_cast<int>(a.g + (b.g - a.g) * t),
        static_cast<int>(a.b + (b.b - a.b) * t)
    };
}

static Color scale(Color c, float s) {
    return {
        std::clamp(static_cast<int>(c.r * s), 0, 255),
        std::clamp(static_cast<int>(c.g * s), 0, 255),
        std::clamp(static_cast<int>(c.b * s), 0, 255)
    };
}

static COLORREF cref(Color c) {
    return RGB(std::clamp(c.r,0,255), std::clamp(c.g,0,255), std::clamp(c.b,0,255));
}

static Color hsv(float hueDeg, float saturation, float value) {
    hueDeg = std::fmod(hueDeg, 360.0f);
    if (hueDeg < 0.0f) hueDeg += 360.0f;
    saturation = std::clamp(saturation, 0.0f, 1.0f);
    value = std::clamp(value, 0.0f, 1.0f);

    const float c = value * saturation;
    const float x = c * (1.0f - std::fabs(std::fmod(hueDeg / 60.0f, 2.0f) - 1.0f));
    const float m = value - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hueDeg < 60.0f)       { r = c; g = x; }
    else if (hueDeg < 120.0f) { r = x; g = c; }
    else if (hueDeg < 180.0f) { g = c; b = x; }
    else if (hueDeg < 240.0f) { g = x; b = c; }
    else if (hueDeg < 300.0f) { r = x; b = c; }
    else                       { r = c; b = x; }

    return {
        static_cast<int>((r + m) * 255.0f),
        static_cast<int>((g + m) * 255.0f),
        static_cast<int>((b + m) * 255.0f)
    };
}

static Color spectrumColor(float t, float brightness = 1.0f) {
    // Low -> high: red, orange, yellow, green, cyan, blue, violet, magenta.
    t = std::clamp(t, 0.0f, 1.0f);
    return hsv(330.0f * t, 0.90f, std::clamp(brightness, 0.0f, 1.0f));
}

static Color spectrumFromPitch(float pitch, float edge, float air, float rms) {
    float t = 0.58f;
    if (pitch > 0.0f) {
        const float lo = std::log2(75.0f);
        const float hi = std::log2(520.0f);
        t = std::clamp((std::log2(pitch) - lo) / (hi - lo), 0.0f, 1.0f);
    }
    Color c = spectrumColor(t, 0.70f + std::clamp(rms * 5.0f, 0.0f, 0.30f));
    c = mix(c, Color{255, 255, 255}, air * 0.25f);
    c = mix(c, Color{255, 70, 38}, edge * 0.12f);
    return c;
}

struct AudioCapture {
    struct Buffer {
        WAVEHDR hdr{};
        std::array<int16_t, kBufferSamples> data{};
    };

    HWAVEIN waveIn = nullptr;
    std::array<Buffer, kBufferCount> buffers{};
    std::array<float, kRingSize> ring{};
    size_t writePos = 0;
    bool filled = false;
    std::mutex mutex;
    std::atomic<bool> running{false};

    static void CALLBACK callback(HWAVEIN hwi, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
        if (msg != WIM_DATA || !instance || !param1) return;
        auto* self = reinterpret_cast<AudioCapture*>(instance);
        auto* hdr = reinterpret_cast<WAVEHDR*>(param1);
        if (!self->running.load()) return;

        const int count = static_cast<int>(hdr->dwBytesRecorded / sizeof(int16_t));
        auto* samples = reinterpret_cast<int16_t*>(hdr->lpData);
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            for (int i = 0; i < count; ++i) {
                self->ring[self->writePos] = samples[i] / 32768.0f;
                self->writePos = (self->writePos + 1) % kRingSize;
                if (self->writePos == 0) self->filled = true;
            }
        }

        hdr->dwBytesRecorded = 0;
        if (self->running.load()) {
            waveInAddBuffer(hwi, hdr, sizeof(WAVEHDR));
        }
    }

    bool start() {
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = kSampleRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

        MMRESULT mm = waveInOpen(
            &waveIn, WAVE_MAPPER, &fmt,
            reinterpret_cast<DWORD_PTR>(&AudioCapture::callback),
            reinterpret_cast<DWORD_PTR>(this),
            CALLBACK_FUNCTION
        );
        if (mm != MMSYSERR_NOERROR) return false;

        running.store(true);
        for (auto& b : buffers) {
            b.hdr.lpData = reinterpret_cast<LPSTR>(b.data.data());
            b.hdr.dwBufferLength = static_cast<DWORD>(b.data.size() * sizeof(int16_t));
            b.hdr.dwFlags = 0;
            b.hdr.dwLoops = 0;
            waveInPrepareHeader(waveIn, &b.hdr, sizeof(WAVEHDR));
            waveInAddBuffer(waveIn, &b.hdr, sizeof(WAVEHDR));
        }
        return waveInStart(waveIn) == MMSYSERR_NOERROR;
    }

    void stop() {
        if (!waveIn) return;
        running.store(false);
        waveInStop(waveIn);
        waveInReset(waveIn);
        for (auto& b : buffers) {
            waveInUnprepareHeader(waveIn, &b.hdr, sizeof(WAVEHDR));
        }
        waveInClose(waveIn);
        waveIn = nullptr;
    }

    ~AudioCapture() { stop(); }

    bool latest(std::array<float, kFftSize>& out) {
        std::lock_guard<std::mutex> lock(mutex);
        const size_t available = filled ? kRingSize : writePos;
        if (available < kFftSize) return false;

        size_t start = (writePos + kRingSize - kFftSize) % kRingSize;
        for (int i = 0; i < kFftSize; ++i) {
            out[i] = ring[(start + i) % kRingSize];
        }
        return true;
    }
};

struct Analysis {
    float rms = 0.0f;
    float pitchHz = 0.0f;
    float edge = 0.0f;
    float air = 0.0f;
    float centroidHz = 0.0f;
    float peakHz = 0.0f;
    float lowEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;
    std::array<float, kBars> bars{};
    std::array<float, kAnchors> anchors{};
};

static void fft(std::array<std::complex<float>, kFftSize>& a) {
    const int n = kFftSize;
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979323846f / len;
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                auto u = a[i + j];
                auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

static float goertzelMagnitude(const std::array<float, kFftSize>& input, float hz) {
    if (hz <= 20.0f || hz >= kSampleRate * 0.48f) return 0.0f;
    const float omega = 2.0f * 3.14159265358979323846f * hz / kSampleRate;
    const float coeff = 2.0f * std::cos(omega);
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;

    for (int i = 0; i < kFftSize; ++i) {
        const float window = 0.5f - 0.5f * std::cos(
            2.0f * 3.14159265358979323846f * i / (kFftSize - 1)
        );
        s0 = input[i] * window + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const float power = std::max(0.0f, s1 * s1 + s2 * s2 - coeff * s1 * s2);
    return 2.0f * std::sqrt(power) / kFftSize;
}

static float anchorFamilyStrength(const std::array<float, kFftSize>& input, float targetHz, float rms) {
    if (rms < 0.004f) return 0.0f;

    auto bestNear = [&](float hz) {
        float best = 0.0f;
        for (float drift : {0.985f, 1.0f, 1.015f}) {
            best = std::max(best, goertzelMagnitude(input, hz * drift));
        }
        return best;
    };

    float direct = bestNear(targetHz);
    float sub = targetHz * 0.5f >= 55.0f ? bestNear(targetHz * 0.5f) * 0.72f : 0.0f;
    float harmonic = targetHz * 2.0f < 6000.0f ? bestNear(targetHz * 2.0f) * 0.60f : 0.0f;
    float mag = std::max(direct, std::max(sub, harmonic));

    const float ratio = mag / (rms + 1e-5f);
    return std::clamp((ratio - 0.06f) / 0.90f, 0.0f, 1.0f);
}

static float estimatePitch(const std::array<float, kFftSize>& x, float rms) {
    if (rms < 0.008f) return 0.0f;

    std::array<float, kFftSize> y{};
    float mean = 0.0f;
    for (float v : x) mean += v;
    mean /= kFftSize;

    for (int i = 0; i < kFftSize; ++i) {
        float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * i / (kFftSize - 1));
        y[i] = (x[i] - mean) * w;
    }

    const int minLag = kSampleRate / 520;
    const int maxLag = kSampleRate / 75;
    float best = 0.0f;
    int bestLag = 0;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        double num = 0.0, d1 = 0.0, d2 = 0.0;
        const int count = kFftSize - lag;
        for (int i = 0; i < count; ++i) {
            const float a = y[i];
            const float b = y[i + lag];
            num += a * b;
            d1 += a * a;
            d2 += b * b;
        }
        const double den = std::sqrt(d1 * d2) + 1e-12;
        const float corr = static_cast<float>(num / den);
        if (corr > best) {
            best = corr;
            bestLag = lag;
        }
    }

    if (best < 0.42f || bestLag <= 0) return 0.0f;
    return static_cast<float>(kSampleRate) / bestLag;
}

static Analysis analyze(const std::array<float, kFftSize>& input) {
    Analysis out{};

    double sumSq = 0.0;
    int crossings = 0;
    for (int i = 0; i < kFftSize; ++i) {
        sumSq += input[i] * input[i];
        if (i && ((input[i - 1] >= 0) != (input[i] >= 0))) ++crossings;
    }
    out.rms = static_cast<float>(std::sqrt(sumSq / kFftSize));
    out.pitchHz = estimatePitch(input, out.rms);

    std::array<std::complex<float>, kFftSize> spec{};
    for (int i = 0; i < kFftSize; ++i) {
        const float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * i / (kFftSize - 1));
        spec[i] = {input[i] * w, 0.0f};
    }
    fft(spec);

    auto magAt = [&](int bin) {
        return std::abs(spec[std::clamp(bin, 0, kFftSize / 2 - 1)]) / (kFftSize * 0.5f);
    };

    double total = 0.0;
    double high = 0.0;
    double air = 0.0;
    double lowBand = 0.0;
    double midBand = 0.0;
    double highBand = 0.0;
    double weightedHz = 0.0;
    float spectralPeakMag = 0.0f;
    float spectralPeakHz = 0.0f;
    for (int k = 1; k < kFftSize / 2; ++k) {
        const float hz = static_cast<float>(k) * kSampleRate / kFftSize;
        const float m = magAt(k);
        if (hz >= 55.0f && hz <= 10000.0f) {
            total += m;
            weightedHz += hz * m;
            if (m > spectralPeakMag) {
                spectralPeakMag = m;
                spectralPeakHz = hz;
            }
        }
        if (hz >= 55.0f && hz < 250.0f) lowBand += m;
        if (hz >= 250.0f && hz < 2000.0f) midBand += m;
        if (hz >= 2000.0f && hz <= 10000.0f) highBand += m;
        if (hz >= 1800.0f && hz <= 7000.0f) high += m;
        if (hz >= 5000.0f && hz <= 10000.0f) air += m;
    }

    const float highRatio = total > 0.0 ? static_cast<float>(high / total) : 0.0f;
    const float airRatio = total > 0.0 ? static_cast<float>(air / total) : 0.0f;
    out.centroidHz = total > 0.0 ? static_cast<float>(weightedHz / total) : 0.0f;
    out.peakHz = spectralPeakHz;
    out.lowEnergy = total > 0.0 ? static_cast<float>(lowBand / total) : 0.0f;
    out.midEnergy = total > 0.0 ? static_cast<float>(midBand / total) : 0.0f;
    out.highEnergy = total > 0.0 ? static_cast<float>(highBand / total) : 0.0f;
    const float zcr = static_cast<float>(crossings) / kFftSize;

    out.edge = std::clamp((highRatio - 0.18f) * 2.7f + (zcr - 0.06f) * 2.0f, 0.0f, 1.0f);
    out.air = std::clamp((airRatio - 0.035f) * 4.0f, 0.0f, 1.0f);

    const float minF = 45.0f;
    const float maxF = 12000.0f;
    for (int b = 0; b < kBars; ++b) {
        float t0 = static_cast<float>(b) / kBars;
        float t1 = static_cast<float>(b + 1) / kBars;
        float f0 = minF * std::pow(maxF / minF, t0);
        float f1 = minF * std::pow(maxF / minF, t1);
        int k0 = std::max(1, static_cast<int>(f0 * kFftSize / kSampleRate));
        int k1 = std::min(kFftSize / 2 - 1, static_cast<int>(f1 * kFftSize / kSampleRate) + 1);
        float peak = 0.0f;
        for (int k = k0; k <= k1; ++k) peak = std::max(peak, magAt(k));
        float db = 20.0f * std::log10(peak + 1e-6f);
        out.bars[b] = std::clamp((db + 70.0f) / 55.0f, 0.0f, 1.0f);
    }

    for (int i = 0; i < kAnchors; ++i) {
        out.anchors[i] = anchorFamilyStrength(input, kAnchorHz[i], out.rms);
    }

    return out;
}

static std::wstring noteName(float hz) {
    if (hz <= 0.0f) return L"--";
    const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)));
    static const wchar_t* names[] = {L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"};
    int idx = ((midi % 12) + 12) % 12;
    int octave = midi / 12 - 1;
    return std::wstring(names[idx]) + std::to_wstring(octave);
}

static Color paletteFromVoice(float pitch, float edge, float air, float rms) {
    const Color burgundy{92, 18, 52};
    const Color violet{122, 58, 188};
    const Color cyan{63, 188, 211};
    const Color silver{211, 222, 230};
    const Color rust{212, 72, 42};

    Color c = violet;
    if (pitch > 0.0f) {
        if (pitch < 200.0f) {
            c = mix(burgundy, violet, std::clamp((pitch - 140.0f) / 60.0f, 0.0f, 1.0f));
        } else if (pitch < 300.0f) {
            c = mix(violet, cyan, (pitch - 200.0f) / 100.0f);
        } else {
            c = mix(cyan, silver, std::clamp((pitch - 300.0f) / 180.0f, 0.0f, 1.0f));
        }
    }

    c = mix(c, rust, edge * 0.58f);
    c = mix(c, silver, air * 0.22f);

    float brightness = 0.52f + std::clamp(rms * 7.0f, 0.0f, 0.48f);
    return scale(c, brightness);
}


enum class VisualMode {
    Weather = 0,
    Spectrum = 1,
    Agnathos = 2
};

enum class WeatherKind {
    Fog = 0,
    Rain,
    Storm,
    Ember,
    Aurora,
    Glare,
    AcidSky
};

struct WeatherPalette {
    const wchar_t* name;
    Color low;
    Color mid;
    Color high;
    Color sky;
};

static WeatherPalette weatherPalette(WeatherKind kind) {
    switch (kind) {
    case WeatherKind::Rain:
        return {L"RAIN", {24, 58, 96}, {52, 114, 166}, {146, 196, 222}, {10, 18, 28}};
    case WeatherKind::Storm:
        return {L"STORM", {50, 18, 74}, {126, 34, 94}, {205, 218, 236}, {15, 8, 22}};
    case WeatherKind::Ember:
        return {L"EMBER", {88, 20, 18}, {212, 58, 24}, {255, 170, 52}, {24, 8, 6}};
    case WeatherKind::Aurora:
        return {L"AURORA", {28, 88, 118}, {62, 170, 158}, {160, 102, 222}, {7, 20, 24}};
    case WeatherKind::Glare:
        return {L"GLARE", {126, 92, 24}, {226, 176, 54}, {244, 236, 198}, {26, 22, 10}};
    case WeatherKind::AcidSky:
        return {L"ACID SKY", {54, 112, 30}, {154, 202, 44}, {202, 72, 210}, {12, 22, 8}};
    case WeatherKind::Fog:
    default:
        return {L"FOG", {52, 58, 74}, {92, 104, 126}, {170, 180, 194}, {12, 13, 18}};
    }
}

static float averageBars(const std::array<float, kBars>& bars, int begin, int end) {
    begin = std::clamp(begin, 0, kBars);
    end = std::clamp(end, begin + 1, kBars);
    float sum = 0.0f;
    for (int i = begin; i < end; ++i) sum += bars[i];
    return sum / static_cast<float>(end - begin);
}

static WeatherKind chooseWeather(const Analysis& a, const std::array<float, kBars>& bars) {
    const float low = averageBars(bars, 0, 24);
    const float mid = averageBars(bars, 24, 48);
    const float high = averageBars(bars, 48, 72);
    const float intensity = std::clamp(a.rms * 8.5f, 0.0f, 1.0f);
    const float voiced = a.pitchHz > 0.0f ? 1.0f : 0.0f;

    std::array<float, 7> score{};
    score[static_cast<int>(WeatherKind::Fog)] =
        (1.0f - intensity) * 0.54f + low * 0.18f + (1.0f - a.air) * 0.18f + (1.0f - a.edge) * 0.10f;
    score[static_cast<int>(WeatherKind::Rain)] =
        mid * 0.32f + high * 0.18f + (1.0f - intensity) * 0.22f + (1.0f - a.edge) * 0.18f + a.air * 0.10f;
    score[static_cast<int>(WeatherKind::Storm)] =
        intensity * 0.42f + low * 0.28f + a.edge * 0.20f + mid * 0.10f;
    score[static_cast<int>(WeatherKind::Ember)] =
        a.edge * 0.44f + intensity * 0.30f + low * 0.16f + mid * 0.10f;
    score[static_cast<int>(WeatherKind::Aurora)] =
        a.air * 0.28f + high * 0.20f + mid * 0.18f + voiced * 0.22f + (1.0f - a.edge) * 0.12f;
    score[static_cast<int>(WeatherKind::Glare)] =
        high * 0.34f + a.air * 0.30f + intensity * 0.26f + voiced * 0.10f;
    score[static_cast<int>(WeatherKind::AcidSky)] =
        a.edge * 0.32f + a.air * 0.24f + high * 0.22f + intensity * 0.12f + std::fabs(high - low) * 0.10f;

    int best = 0;
    for (int i = 1; i < static_cast<int>(score.size()); ++i) {
        if (score[i] > score[best]) best = i;
    }
    return static_cast<WeatherKind>(best);
}

static Color weatherBandColor(const WeatherPalette& p, float ft) {
    ft = std::clamp(ft, 0.0f, 1.0f);
    if (ft < 0.52f) return mix(p.low, p.mid, ft / 0.52f);
    return mix(p.mid, p.high, (ft - 0.52f) / 0.48f);
}

struct ChameleonState {
    float hue = 262.0f;
    float saturation = 0.74f;
    float value = 0.72f;
    float memoryLow = 0.0f;
    float memoryMid = 0.0f;
    float memoryHigh = 0.0f;
    float memoryEdge = 0.0f;
    float memoryAir = 0.0f;
    float memoryIntensity = 0.0f;
    float memoryCentroid = 0.0f;
    float anchorInfluence = 0.0f;
    int activeAnchor = -1;
    int secondaryAnchor = -1;
    float secondaryAnchorStrength = 0.0f;
};

static float hueDistance(float from, float to) {
    float d = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return d;
}

static float approachHue(float current, float target, float amount) {
    current += hueDistance(current, target) * std::clamp(amount, 0.0f, 1.0f);
    if (current < 0.0f) current += 360.0f;
    if (current >= 360.0f) current -= 360.0f;
    return current;
}

static float circularTargetHue(const Analysis& a, const std::array<float, kAnchors>& anchors) {
    float x = 0.0f;
    float y = 0.0f;
    float weight = 0.0f;

    auto addHue = [&](float hue, float w) {
        const float rad = hue * 3.14159265358979323846f / 180.0f;
        x += std::cos(rad) * w;
        y += std::sin(rad) * w;
        weight += w;
    };

    const float intensity = std::clamp(a.rms * 8.0f, 0.0f, 1.0f);
    const float centroidNorm = std::clamp(
        (std::log2(std::max(a.centroidHz, 180.0f)) - std::log2(180.0f)) /
        (std::log2(7000.0f) - std::log2(180.0f)), 0.0f, 1.0f
    );

    if (a.pitchHz > 0.0f) {
        const float pitchNorm = std::clamp(
            (std::log2(a.pitchHz) - std::log2(75.0f)) /
            (std::log2(520.0f) - std::log2(75.0f)), 0.0f, 1.0f
        );
        addHue(250.0f + pitchNorm * 165.0f, 1.10f);
    }

    addHue(286.0f, a.lowEnergy * 1.7f);
    addHue(205.0f, a.midEnergy * 1.25f);
    addHue(44.0f, a.highEnergy * 1.10f);
    addHue(8.0f, a.edge * 0.95f);
    addHue(188.0f, a.air * 0.90f);
    addHue(40.0f + centroidNorm * 220.0f, 0.58f + intensity * 0.28f);

    for (int i = 0; i < kAnchors; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kAnchors - 1);
        const float anchorHue = std::fmod(330.0f * t + 18.0f, 360.0f);
        addHue(anchorHue, anchors[i] * 0.95f);
    }

    if (weight <= 0.0001f) return 262.0f;
    float hue = std::atan2(y, x) * 180.0f / 3.14159265358979323846f;
    if (hue < 0.0f) hue += 360.0f;
    return hue;
}

static Color chameleonColor(const ChameleonState& c, float localFrequencyT, float activity) {
    const float spread = (localFrequencyT - 0.5f) * 94.0f;
    const float hue = c.hue + spread;
    const float sat = std::clamp(c.saturation + activity * 0.10f, 0.0f, 1.0f);
    const float val = std::clamp(c.value * (0.58f + activity * 0.68f), 0.0f, 1.0f);
    return hsv(hue, sat, val);
}

static int logBarIndexForHz(float hz) {
    const float minF = 45.0f;
    const float maxF = 12000.0f;
    const float t = std::clamp(
        std::log(hz / minF) / std::log(maxF / minF),
        0.0f, 1.0f
    );
    return std::clamp(static_cast<int>(std::lround(t * (kBars - 1))), 0, kBars - 1);
}

AudioCapture gAudio;
Analysis gAnalysis{};
std::array<float, kBars> gSmoothBars{};
std::array<float, kAnchors> gAnchorSmooth{};
ChameleonState gChameleon{};
bool gMicOk = false;
bool gFrozen = false;
VisualMode gVisualMode = VisualMode::Weather;
WeatherKind gWeather = WeatherKind::Fog;
WeatherKind gWeatherCandidate = WeatherKind::Fog;
int gWeatherCandidateFrames = 0;
float gPreviousRms = 0.0f;
float gFlash = 0.0f;

HFONT makeFont(int px, int weight) {
    return CreateFontW(
        -px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
}

void drawTextSimple(HDC dc, const std::wstring& text, int x, int y, int w, int h, int size, int weight, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HFONT font = makeFont(size, weight);
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT r{x, y, x + w, y + h};
    DrawTextW(dc, text.c_str(), -1, &r, flags);
    SelectObject(dc, old);
    DeleteObject(font);
}

void paintScene(HWND hwnd, HDC target) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int W = rc.right;
    const int H = rc.bottom;

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, std::max(W,1), std::max(H,1));
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(dc, bmp));

    const WeatherPalette weather = weatherPalette(gWeather);
    Color background{7, 7, 10};
    if (gVisualMode == VisualMode::Weather) {
        const Color climate = hsv(gChameleon.hue, gChameleon.saturation * 0.62f, 0.23f + gChameleon.value * 0.12f);
        background = mix(background, climate, 0.64f);
        background = mix(background, Color{255,255,255}, gFlash * 0.09f);
    }
    HBRUSH bg = CreateSolidBrush(cref(background));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    const Color current =
        gVisualMode == VisualMode::Spectrum
            ? spectrumFromPitch(gAnalysis.pitchHz, gAnalysis.edge, gAnalysis.air, gAnalysis.rms)
            : gVisualMode == VisualMode::Agnathos
                ? paletteFromVoice(gAnalysis.pitchHz, gAnalysis.edge, gAnalysis.air, gAnalysis.rms)
                : hsv(gChameleon.hue, gChameleon.saturation, gChameleon.value);
    const Color burgundy{92, 18, 52};
    const Color violet{122, 58, 188};
    const Color cyan{63, 188, 211};
    const Color silver{211, 222, 230};
    const Color rust{212, 72, 42};

    drawTextSimple(dc, L"AGNATHOS / VOX", 28, 16, 250, 32, 20, FW_SEMIBOLD, RGB(225,225,230));

    std::wstring modeLabel;
    if (gVisualMode == VisualMode::Weather) modeLabel = std::wstring(L"CHAMELEON / ") + weather.name;
    else if (gVisualMode == VisualMode::Spectrum) modeLabel = L"SPECTRUM";
    else modeLabel = L"AGNATHOS";
    drawTextSimple(dc, modeLabel, 260, 16, 300, 32, 12, FW_SEMIBOLD, cref(current));
    drawTextSimple(dc, gMicOk ? (gFrozen ? L"FROZEN" : L"LIVE INPUT") : L"MIC OFFLINE",
                   W - 220, 16, 190, 32, 14, FW_SEMIBOLD,
                   gMicOk ? RGB(165,170,178) : RGB(225,85,72),
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // One analyzer below; the split lives only in the numeric HUD.
    std::wstring freqValue = gAnalysis.peakHz > 0.0f
        ? std::to_wstring(static_cast<int>(std::lround(gAnalysis.peakHz))) + L" Hz"
        : L"-- Hz";
    std::wstring pitchValue = gAnalysis.pitchHz > 0.0f
        ? std::to_wstring(static_cast<int>(std::lround(gAnalysis.pitchHz))) + L" Hz  " + noteName(gAnalysis.pitchHz)
        : L"-- Hz  --";

    std::wstring dominant = L"--";
    std::wstring secondary = L"--";
    if (gChameleon.activeAnchor >= 0) {
        const int confidence = static_cast<int>(std::lround(
            std::clamp(gChameleon.anchorInfluence, 0.0f, 1.0f) * 100.0f));
        dominant = std::wstring(kAnchorNames[gChameleon.activeAnchor]) + L" Hz  " +
            std::to_wstring(confidence) + L"%";
    }
    if (gChameleon.secondaryAnchor >= 0) {
        const int confidence = static_cast<int>(std::lround(
            std::clamp(gChameleon.secondaryAnchorStrength, 0.0f, 1.0f) * 100.0f));
        secondary = std::wstring(kAnchorNames[gChameleon.secondaryAnchor]) + L" Hz  " +
            std::to_wstring(confidence) + L"%";
    }

    const int colGap = 18;
    const int colW = std::max(210, (W - 56 - colGap) / 2);
    const int leftX = 28;
    const int rightX = 28 + colW + colGap;

    drawTextSimple(dc, L"FREQUENCY ANALYSIS", leftX, 58, colW, 20, 11, FW_SEMIBOLD, RGB(118,122,132));
    drawTextSimple(dc, freqValue, leftX, 79, colW, 42, 31, FW_LIGHT, cref(current));
    drawTextSimple(dc, std::wstring(L"PITCH  ") + pitchValue, leftX, 118, colW, 25, 13, FW_SEMIBOLD, RGB(178,180,188));

    drawTextSimple(dc, L"RESONANCE ANALYSIS", rightX, 58, colW, 20, 11, FW_SEMIBOLD, RGB(118,122,132));
    drawTextSimple(dc, dominant, rightX, 79, colW, 42, 31, FW_LIGHT,
                   cref(hsv(gChameleon.hue + 34.0f, 0.78f, 0.96f)));
    drawTextSimple(dc, std::wstring(L"SECONDARY  ") + secondary, rightX, 118, colW, 25, 13, FW_SEMIBOLD, RGB(178,180,188));

    const int lowPct = static_cast<int>(std::lround(std::clamp(gAnalysis.lowEnergy, 0.0f, 1.0f) * 100.0f));
    const int midPct = static_cast<int>(std::lround(std::clamp(gAnalysis.midEnergy, 0.0f, 1.0f) * 100.0f));
    const int highPct = static_cast<int>(std::lround(std::clamp(gAnalysis.highEnergy, 0.0f, 1.0f) * 100.0f));
    std::wstring mixText = L"LOW " + std::to_wstring(lowPct) + L"%   MID " +
        std::to_wstring(midPct) + L"%   HIGH " + std::to_wstring(highPct) +
        L"%   CENTROID " + std::to_wstring(static_cast<int>(std::lround(gAnalysis.centroidHz))) + L" Hz";
    drawTextSimple(dc, mixText, 28, 151, W - 56, 24, 12, FW_NORMAL, RGB(134,138,148));

    int meterY = 184;
    RECT meterBg{28, meterY, W - 28, meterY + 3};
    HBRUSH mb = CreateSolidBrush(RGB(34,34,42));
    FillRect(dc, &meterBg, mb);
    DeleteObject(mb);
    const float level = std::clamp(gAnalysis.rms * 8.0f, 0.0f, 1.0f);
    RECT meter{28, meterY, 28 + static_cast<int>((W - 56) * level), meterY + 3};
    HBRUSH ml = CreateSolidBrush(cref(current));
    FillRect(dc, &meter, ml);
    DeleteObject(ml);

    const int barBottom = H - 54;
    const int barTop = meterY + 28;
    const int usableH = std::max(80, barBottom - barTop);
    const int gap = 2;
    const float totalW = static_cast<float>(W - 56);
    const float bw = totalW / kBars;

    for (int i = 0; i < kBars; ++i) {
        const float v = std::clamp(gSmoothBars[i], 0.0f, 1.0f);
        const int bh = static_cast<int>(v * usableH);
        const int x0 = 28 + static_cast<int>(i * bw);
        const int x1 = 28 + static_cast<int>((i + 1) * bw) - gap;
        const float ft = static_cast<float>(i) / (kBars - 1);

        Color barColor;
        if (gVisualMode == VisualMode::Weather) {
            float anchorGlow = 0.0f;
            Color anchorColor = chameleonColor(gChameleon, ft, v);

            for (int a = 0; a < kAnchors; ++a) {
                const int anchorBar = logBarIndexForHz(kAnchorHz[a]);
                const int distance = std::abs(i - anchorBar);
                if (distance <= 2) {
                    const float falloff = distance == 0 ? 1.0f : (distance == 1 ? 0.48f : 0.18f);
                    const float pull = gAnchorSmooth[a] * falloff;
                    if (pull > anchorGlow) {
                        anchorGlow = pull;
                        const float anchorT = static_cast<float>(a) / static_cast<float>(kAnchors - 1);
                        anchorColor = hsv(18.0f + anchorT * 330.0f, 0.92f, 1.0f);
                    }
                }
            }

            barColor = chameleonColor(gChameleon, ft, v);
            barColor = mix(barColor, anchorColor, std::clamp(anchorGlow * 0.66f, 0.0f, 0.66f));
            if (anchorGlow > 0.0f) {
                barColor = mix(barColor, Color{255,255,255}, std::clamp(anchorGlow * 0.16f, 0.0f, 0.16f));
            }

            // Sudden vocal/beat attacks read as a brief white "lightning" edge.
            const float flashPull = gFlash * (0.16f + ft * 0.34f) * v;
            barColor = mix(barColor, Color{245, 248, 255}, flashPull);
        } else if (gVisualMode == VisualMode::Spectrum) {
            barColor = spectrumColor(ft, 1.0f);
            if (ft > 0.88f) {
                barColor = mix(barColor, silver, ((ft - 0.88f) / 0.12f) * gAnalysis.air * 0.55f);
            }
        } else {
            if (ft < 0.45f) barColor = mix(burgundy, current, ft / 0.45f);
            else if (ft < 0.75f) barColor = mix(current, violet, (ft - 0.45f) / 0.30f * 0.35f);
            else barColor = mix(current, cyan, (ft - 0.75f) / 0.25f * 0.62f);
        }

        const float lit = 0.58f + v * 0.62f;
        HBRUSH br = CreateSolidBrush(cref(scale(barColor, lit)));
        RECT b{x0, barBottom - bh, std::max(x0 + 1, x1), barBottom};
        FillRect(dc, &b, br);
        DeleteObject(br);
    }

    std::wstring foot = L"ONE BLENDED ANALYZER   \u2022   TAB CHAMELEON / SPECTRUM / AGNATHOS   \u2022   SPACE FREEZE   \u2022   ESC QUIT";
    drawTextSimple(dc, foot, 28, H - 42, W - 56, 26, 12, FW_NORMAL, RGB(116,118,126));

    BitBlt(target, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, kTimerId, 16, nullptr);
        return 0;

    case WM_TIMER:
        if (!gFrozen && gMicOk) {
            std::array<float, kFftSize> samples{};
            if (gAudio.latest(samples)) {
                Analysis next = analyze(samples);
                gAnalysis.rms = gAnalysis.rms * 0.68f + next.rms * 0.32f;
                if (next.pitchHz > 0.0f) {
                    if (gAnalysis.pitchHz <= 0.0f) gAnalysis.pitchHz = next.pitchHz;
                    else gAnalysis.pitchHz = gAnalysis.pitchHz * 0.72f + next.pitchHz * 0.28f;
                } else if (gAnalysis.rms < 0.010f) {
                    gAnalysis.pitchHz = 0.0f;
                }
                gAnalysis.edge = gAnalysis.edge * 0.72f + next.edge * 0.28f;
                gAnalysis.air = gAnalysis.air * 0.72f + next.air * 0.28f;
                gAnalysis.centroidHz = gAnalysis.centroidHz * 0.82f + next.centroidHz * 0.18f;
                gAnalysis.peakHz = gAnalysis.peakHz * 0.68f + next.peakHz * 0.32f;
                gAnalysis.lowEnergy = gAnalysis.lowEnergy * 0.82f + next.lowEnergy * 0.18f;
                gAnalysis.midEnergy = gAnalysis.midEnergy * 0.82f + next.midEnergy * 0.18f;
                gAnalysis.highEnergy = gAnalysis.highEnergy * 0.82f + next.highEnergy * 0.18f;
                for (int i = 0; i < kBars; ++i) {
                    const float fall = 0.035f;
                    if (next.bars[i] > gSmoothBars[i]) gSmoothBars[i] = gSmoothBars[i] * 0.55f + next.bars[i] * 0.45f;
                    else gSmoothBars[i] = std::max(next.bars[i], gSmoothBars[i] - fall);
                }

                const float attack = std::max(0.0f, gAnalysis.rms - gPreviousRms);
                gFlash = std::max(gFlash * 0.84f, std::clamp(attack * 18.0f, 0.0f, 1.0f));
                gPreviousRms = gAnalysis.rms;

                for (int i = 0; i < kAnchors; ++i) {
                    const float attackAlpha = next.anchors[i] > gAnchorSmooth[i] ? 0.28f : 0.055f;
                    gAnchorSmooth[i] += (next.anchors[i] - gAnchorSmooth[i]) * attackAlpha;
                }

                int strongestAnchor = -1;
                int secondAnchor = -1;
                float strongestValue = 0.0f;
                float secondValue = 0.0f;
                for (int i = 0; i < kAnchors; ++i) {
                    const float v = gAnchorSmooth[i];
                    if (v > strongestValue) {
                        secondValue = strongestValue;
                        secondAnchor = strongestAnchor;
                        strongestValue = v;
                        strongestAnchor = i;
                    } else if (v > secondValue) {
                        secondValue = v;
                        secondAnchor = i;
                    }
                }
                gChameleon.activeAnchor = strongestValue > 0.10f ? strongestAnchor : -1;
                gChameleon.secondaryAnchor = secondValue > 0.08f ? secondAnchor : -1;
                gChameleon.secondaryAnchorStrength = secondValue;
                gChameleon.anchorInfluence += (strongestValue - gChameleon.anchorInfluence) * 0.045f;

                // Long-ish memory: the climate remembers the previous phrase instead of repainting every frame.
                constexpr float memoryAlpha = 0.016f;
                gChameleon.memoryLow += (gAnalysis.lowEnergy - gChameleon.memoryLow) * memoryAlpha;
                gChameleon.memoryMid += (gAnalysis.midEnergy - gChameleon.memoryMid) * memoryAlpha;
                gChameleon.memoryHigh += (gAnalysis.highEnergy - gChameleon.memoryHigh) * memoryAlpha;
                gChameleon.memoryEdge += (gAnalysis.edge - gChameleon.memoryEdge) * memoryAlpha;
                gChameleon.memoryAir += (gAnalysis.air - gChameleon.memoryAir) * memoryAlpha;
                const float intensity = std::clamp(gAnalysis.rms * 8.0f, 0.0f, 1.0f);
                gChameleon.memoryIntensity += (intensity - gChameleon.memoryIntensity) * memoryAlpha;
                gChameleon.memoryCentroid += (gAnalysis.centroidHz - gChameleon.memoryCentroid) * memoryAlpha;

                const float targetHue = circularTargetHue(gAnalysis, gAnchorSmooth);
                gChameleon.hue = approachHue(gChameleon.hue, targetHue, 0.045f);
                const float targetSat = std::clamp(
                    0.48f + gChameleon.memoryEdge * 0.22f +
                    gChameleon.anchorInfluence * 0.20f +
                    std::fabs(gChameleon.memoryHigh - gChameleon.memoryLow) * 0.22f,
                    0.42f, 0.98f
                );
                const float targetValue = std::clamp(
                    0.42f + gChameleon.memoryIntensity * 0.38f +
                    gChameleon.memoryAir * 0.16f +
                    gChameleon.anchorInfluence * 0.10f,
                    0.38f, 1.0f
                );
                gChameleon.saturation += (targetSat - gChameleon.saturation) * 0.030f;
                gChameleon.value += (targetValue - gChameleon.value) * 0.040f;

                // Weather is now only a descriptive label for the continuous chameleon field.
                const WeatherKind candidate = chooseWeather(gAnalysis, gSmoothBars);
                if (candidate == gWeather) {
                    gWeatherCandidate = candidate;
                    gWeatherCandidateFrames = 0;
                } else if (candidate == gWeatherCandidate) {
                    ++gWeatherCandidateFrames;
                    if (gWeatherCandidateFrames >= 18) {
                        gWeather = candidate;
                        gWeatherCandidateFrames = 0;
                    }
                } else {
                    gWeatherCandidate = candidate;
                    gWeatherCandidateFrames = 1;
                }
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_SPACE) {
            gFrozen = !gFrozen;
            return 0;
        }
        if (wParam == VK_TAB) {
            const int nextMode = (static_cast<int>(gVisualMode) + 1) % 3;
            gVisualMode = static_cast<VisualMode>(nextMode);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        paintScene(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        gAudio.stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    SetProcessDPIAware();

    const wchar_t* cls = L"AgnathosVoxWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = cls;

    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, cls, L"AGNATHOS / VOX",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1040, 650,
        nullptr, nullptr, instance, nullptr
    );
    if (!hwnd) return 2;

    gMicOk = gAudio.start();

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
