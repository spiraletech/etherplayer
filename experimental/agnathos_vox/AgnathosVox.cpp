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
constexpr UINT_PTR kTimerId = 1;

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
    std::array<float, kBars> bars{};
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
    for (int k = 1; k < kFftSize / 2; ++k) {
        const float hz = static_cast<float>(k) * kSampleRate / kFftSize;
        const float m = magAt(k);
        if (hz >= 90.0f && hz <= 10000.0f) total += m;
        if (hz >= 1800.0f && hz <= 7000.0f) high += m;
        if (hz >= 5000.0f && hz <= 10000.0f) air += m;
    }

    const float highRatio = total > 0.0 ? static_cast<float>(high / total) : 0.0f;
    const float airRatio = total > 0.0 ? static_cast<float>(air / total) : 0.0f;
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

AudioCapture gAudio;
Analysis gAnalysis{};
std::array<float, kBars> gSmoothBars{};
bool gMicOk = false;
bool gFrozen = false;
bool gSpectrumMode = true;

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

    HBRUSH bg = CreateSolidBrush(RGB(7, 7, 10));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    const Color current = gSpectrumMode
        ? spectrumFromPitch(gAnalysis.pitchHz, gAnalysis.edge, gAnalysis.air, gAnalysis.rms)
        : paletteFromVoice(gAnalysis.pitchHz, gAnalysis.edge, gAnalysis.air, gAnalysis.rms);
    const Color burgundy{92, 18, 52};
    const Color violet{122, 58, 188};
    const Color cyan{63, 188, 211};
    const Color silver{211, 222, 230};
    const Color rust{212, 72, 42};

    drawTextSimple(dc, L"AGNATHOS / VOX", 28, 18, 250, 34, 20, FW_SEMIBOLD, RGB(225,225,230));
    drawTextSimple(dc, gSpectrumMode ? L"SPECTRUM" : L"AGNATHOS", 260, 18, 180, 34, 12, FW_SEMIBOLD, cref(current));
    drawTextSimple(dc, gMicOk ? (gFrozen ? L"FROZEN" : L"LIVE INPUT") : L"MIC OFFLINE",
                   W - 220, 18, 190, 34, 14, FW_SEMIBOLD,
                   gMicOk ? RGB(165,170,178) : RGB(225,85,72),
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    std::wstring big = L"-- Hz";
    if (gAnalysis.pitchHz > 0.0f) {
        big = std::to_wstring(static_cast<int>(std::lround(gAnalysis.pitchHz))) + L" Hz";
    }
    drawTextSimple(dc, big, 28, 66, W - 56, 72, 48, FW_LIGHT, cref(current));
    drawTextSimple(dc, noteName(gAnalysis.pitchHz), 32, 130, 150, 38, 22, FW_SEMIBOLD, RGB(178,180,188));

    const int swY = 182;
    const int swH = 24;
    const int swW = std::max(50, (W - 56) / 5);
    std::array<Color,5> swatches{};
    if (gSpectrumMode) {
        swatches = {
            spectrumColor(0.00f, 0.85f),
            spectrumColor(0.25f, 0.90f),
            spectrumColor(0.50f, 0.95f),
            spectrumColor(0.75f, 1.00f),
            spectrumColor(1.00f, 1.00f)
        };
    } else {
        swatches = {
            scale(current, 0.58f),
            current,
            mix(current, cyan, 0.45f + gAnalysis.air * 0.35f),
            mix(current, rust, 0.45f + gAnalysis.edge * 0.45f),
            mix(current, silver, 0.65f)
        };
    }
    for (int i = 0; i < 5; ++i) {
        RECT s{28 + i * swW, swY, 28 + (i + 1) * swW - 4, swY + swH};
        HBRUSH br = CreateSolidBrush(cref(swatches[i]));
        FillRect(dc, &s, br);
        DeleteObject(br);
    }

    int meterY = swY + 42;
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
        if (gSpectrumMode) {
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

    std::wstring foot = L"DEFAULT MICROPHONE   \u2022   TAB MODE   \u2022   SPACE FREEZE   \u2022   ESC QUIT";
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
                for (int i = 0; i < kBars; ++i) {
                    const float fall = 0.035f;
                    if (next.bars[i] > gSmoothBars[i]) gSmoothBars[i] = gSmoothBars[i] * 0.55f + next.bars[i] * 0.45f;
                    else gSmoothBars[i] = std::max(next.bars[i], gSmoothBars[i] - fall);
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
            gSpectrumMode = !gSpectrumMode;
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
