#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>
#include <mutex>
#include <thread>
#include <cstdio>
#include <stdexcept>
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <SDL.h>
#undef main
#include "input/miracle.h"
#include "input/share.h"

uint32 timestamp = 0;
uint64 timestampbase = 0;
namespace nesProtocol {
#include "input/miracle.cpp"
}

static unsigned queuedBytes;
static double peak;
static int opens, closes;
static std::thread::id audioThread;
static FCEUMiracleOutputByteHook byteHook;
static FCEUMiracleOutputResetHook resetHook;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void checkThread() {
    require(std::this_thread::get_id() == audioThread, "Synth accessed off GUI thread");
}
static SDL_AudioDeviceID testOpen(const char*, int, const SDL_AudioSpec* spec, SDL_AudioSpec* obtained, int) {
    checkThread(); ++opens; *obtained = *spec; return 1;
}
static void testPause(SDL_AudioDeviceID, int) { checkThread(); }
static void testClear(SDL_AudioDeviceID) { checkThread(); queuedBytes = 0; }
static void testClose(SDL_AudioDeviceID) { checkThread(); ++closes; }
static Uint32 testSize(SDL_AudioDeviceID) { checkThread(); return queuedBytes; }
static int testQueue(SDL_AudioDeviceID, const void* data, Uint32 length) {
    checkThread(); queuedBytes += length;
    auto samples = static_cast<const float*>(data);
    for (unsigned i = 0; i < length / sizeof(float); ++i)
        peak = std::max(peak, std::abs(static_cast<double>(samples[i])));
    return 0;
}
static Uint32 testWasInit(Uint32 flags) { return flags; }
static int testInit(Uint32) { return 0; }
#define SDL_OpenAudioDevice testOpen
#define SDL_PauseAudioDevice testPause
#define SDL_ClearQueuedAudio testClear
#define SDL_CloseAudioDevice testClose
#define SDL_GetQueuedAudioSize testSize
#define SDL_QueueAudio testQueue
#define SDL_WasInit testWasInit
#define SDL_InitSubSystem testInit
#define SDL_memset std::memset

void FCEU_SetMiraclePianoOutputHooks(FCEUMiracleOutputByteHook bytes, FCEUMiracleOutputResetHook reset) {
    byteHook = bytes; resetHook = reset;
    nesProtocol::FCEU_SetMiraclePianoOutputHooks(bytes, reset);
}

// Generated verbatim from MiracleWindow.cpp by the accompanying runner.
void setMiraclePianoAudioVolume(int percent);
#include "miracle-audio-under-test.hpp"

static void tick() {
    queuedBytes = 0; peak = 0;
    int32 samples[512] = {};
    mixMiraclePianoAudio(samples, 512, 44100);
    for (auto sample : samples) peak = std::max(peak, std::abs(sample / 32768.0));
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("FCEUXAudioRegression");
    app.setApplicationName("IsolatedCheck");
    audioThread = std::this_thread::get_id();
    try {
        initMiraclePianoAudio();
        require(byteHook && resetHook, "Hooks missing before viewer opens");
        require(opens == 0, "Device opened before any audio request");
        initMiraclePianoAudio();
        std::thread emulation([] {
            auto port = nesProtocol::FCEU_InitMiraclePiano(1);
            for (uint8 byte : {0x90, 60, 100}) {
                port->Write(0); timestamp += 10;
                port->Write(1); timestamp += 66;
                for (int bit = 7; bit >= 0; --bit) {
                    port->Write((byte >> bit) & 1);
                    timestamp += bit ? 15 : 12;
                }
                port->Write(0);
            }
        });
        emulation.join();
        tick(); tick();
        require(opens == 0 && peak > 0.01, "Game note must mix without opening another SDL device");
        g_miraclePreviewSynth.releaseLocal();
        tick();
        require(peak > 0.01, "Closing viewer silenced game playback");
        resetHook(); tick();
        require(peak == 0, "Reset left game note sounding");
        FCEUMiraclePianoInputState state = {};
        state.pressedKeys[24] = 1; state.velocity[24] = 100;
        queuedBytes = 0; peak = 0; g_miraclePreviewSynth.update(state);
        require(peak > 0.01, "Local keyboard stopped working");
        g_miraclePreviewSynth.releaseLocal();
        queuedBytes = 0; peak = 0; g_miraclePreviewSynth.service();
        require(peak == 0, "Viewer close left a local note sounding");
        byteHook(0x90); byteHook(64); byteHook(100); tick(); tick();
        require(peak > 0.01, "Playback failed after reset");
        byteHook(0xB0); byteHook(120); byteHook(0); tick(); tick();
        require(peak == 0, "All-sound-off ignored");
        auto gamePeak = [](int volume) {
            resetMiraclePianoAudio();
            setMiraclePianoAudioVolume(volume);
            byteHook(0x90); byteHook(60); byteHook(100);
            tick();
            return peak;
        };
        const double normal = gamePeak(100);
        const double loud = gamePeak(400);
        require(normal > 0.01 && std::abs(loud / normal - 4.0) < 0.01,
            "400 percent volume does not reach game audio");
        require(gamePeak(0) == 0, "Zero piano volume still audible");
        resetMiraclePianoAudio();
        setMiraclePianoAudioVolume(400);
        for (int channel = 0; channel < 16; ++channel) {
            byteHook(0x90 | channel); byteHook(60); byteHook(127);
        }
        int32 loudChord[2048] = {};
        mixMiraclePianoAudio(loudChord, 2048, 44100);
        int chordPeak = 0;
        for (auto sample : loudChord) chordPeak = std::max(chordPeak, std::abs(sample));
        require(chordPeak > 5000 && chordPeak < 25000, "Dense chord exceeds linear mix headroom");
        resetMiraclePianoAudio();
        setMiraclePianoAudioVolume(100);
        for (int channel = 0; channel < 16; ++channel) {
            byteHook(0x90 | channel); byteHook(60); byteHook(127);
        }
        int32 quietChord[2048] = {};
        mixMiraclePianoAudio(quietChord, 2048, 44100);
        for (int i = 0; i < 2048; ++i)
            require(std::abs(loudChord[i] - 4 * quietChord[i]) <= 4,
                "Dense chord gain reshapes the waveform");
        resetMiraclePianoAudio();
        setMiraclePianoAudioVolume(400);
        byteHook(0xC0); byteHook(113);
        byteHook(0x90); byteHook(60); byteHook(127);
        int32 fallback[2048] = {};
        mixMiraclePianoAudio(fallback, 2048, 44100);
        int maxJump = 0;
        for (int i = 1; i < 2048; ++i)
            maxJump = std::max(maxJump, std::abs(fallback[i] - fallback[i - 1]));
        require(maxJump < 2000, "Game fallback instrument still has abrupt square-wave edges");
        std::printf("Dense chord peak: %d/32767; fallback largest sample step: %d\n", chordPeak, maxJump);
        resetMiraclePianoAudio();
        int32 ordinaryAudio[4] = {1234, -4321, 0, 32767};
        mixMiraclePianoAudio(ordinaryAudio, 4, 48000);
        require(ordinaryAudio[0] == 1234 && ordinaryAudio[1] == -4321 && ordinaryAudio[3] == 32767,
            "Inactive piano changed NES audio");
        auto port = nesProtocol::FCEU_InitMiraclePiano(1);
        auto receive = [port]() {
            port->Write(1); timestamp += 12; port->Write(0); timestamp += 21;
            require(port->Read(1) == 1, "Keyboard byte unavailable");
            uint8 result = 0;
            for (int i = 0; i < 8; ++i) {
                timestamp += 16;
                result = (result << 1) | (port->Read(1) ^ 1);
            }
            return result;
        };
        const uint8 identity[] = {0xF0, 0, 0, 0x42, 1, 5, 1, 0x0A, 0xF7};
        for (auto expected : identity) require(receive() == expected, "Identity reply changed");
        port->Update(1, &state, 0);
        require(receive() == 0x90 && receive() == 60 && receive() == 100,
            "Keyboard-to-game note input regressed");
        resetMiraclePianoAudio(); tick();
        shutdownMiraclePianoAudio();
        require(!byteHook && !resetHook && closes == 1, "Audio shutdown incomplete");
        std::puts("PASS: NES write-only serial-to-audio, thread handoff, keyboard-to-NES input, local sound, reset, all-sound-off, shutdown");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        shutdownMiraclePianoAudio();
        return 1;
    }
}
