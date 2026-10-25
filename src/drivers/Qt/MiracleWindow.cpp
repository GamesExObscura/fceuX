#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>
#include <mutex>
#include <QCoreApplication>

#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTimer>
#include <QTouchEvent>
#include <QVBoxLayout>

#include "Qt/MiracleWindow.h"
#include "Qt/sdl.h"

static MiraclePianoDialog_t* miracleWindow = nullptr;

namespace {
constexpr int kMiracleDefaultVolume = 220;

enum MiraclePreviewInstrument
{
	PreviewPiano = 0,
	PreviewHarpsichord,
	PreviewOrgan,
	PreviewVibraphone,
	PreviewElectricPiano,
	PreviewSynth
};

class MiraclePreviewSynth
{
	public:
		explicit MiraclePreviewSynth(bool previewDevice = true) : usePreviewDevice(previewDevice) {}

		void mixGameAudio(int32* samples, int count, int rate)
		{
			if (!samples || count <= 0 || rate <= 0) return;
			struct Voice { int channel, note; double step; };
			std::vector<Voice> voices;
			for (int channel = 0; channel < 16; ++channel)
				for (int note = 0; note < 128; ++note)
					if (remoteSounding[channel][note])
						voices.push_back({channel, note, frequencyForNote(note) / rate});
			if (voices.empty()) return;
			std::vector<double> piano(count, 0.0);
			double headroom = 1.0;
			for (int i = 0; i < count; ++i)
			{
				double mixed = 0;
				double voiceWeight = 0;
				for (const Voice& voice : voices)
				{
						const int channel = voice.channel, note = voice.note;
						if (!remoteSounding[channel][note]) continue;
						double& phase = remotePhases[channel][note];
						phase += voice.step;
						phase -= floor(phase);
						double& attack = remoteAttack[channel][note];
						attack = std::min(1.0, attack + 1.0 / (0.004 * rate));
						const double level = remoteLevels[channel][note] * noteCompensation(note) * attack;
						voiceWeight += level;
						mixed += gameWaveform(phase, note, remoteProgram[channel], voice.step) * level;
						advanceRemoteLevel(remoteLevels[channel][note], remoteSounding[channel][note], remoteSustain[channel], remoteProgram[channel]);
				}
				vibratoPhase += 5.6 / rate;
				vibratoPhase -= floor(vibratoPhase);
				// Budget the sum of voice amplitudes, rather than clipping dense chords.
				piano[i] = mixed * 0.18 * outputGain / std::max(1.0, voiceWeight) * 32767.0;
				if (piano[i] > 0) headroom = std::min(headroom, (32767.0 - samples[i]) / piano[i]);
				if (piano[i] < 0) headroom = std::min(headroom, (-32768.0 - samples[i]) / piano[i]);
			}
			// A single linear gain preserves waveform shape if NES audio needs extra room.
			headroom = std::max(0.0, headroom);
			for (int i = 0; i < count; ++i) samples[i] += (int32)(piano[i] * headroom);
		}
		void setVolume(float gain)
		{
			outputGain = std::max(0.0f, std::min(4.0f, gain));
		}

		~MiraclePreviewSynth()
		{
			shutdown();
		}

		void update(const FCEUMiraclePianoInputState& state)
		{
			setLocalState(state);
			ensureOpen();
			if (!opened)
			{
				return;
			}
			queueMoreAudio();
		}

		void pushByte(uint8 data)
		{
			ensureOpen();

			if (data == 0xFF)
			{
				resetRemote();
				parserStatus = 0;
				parserNeeded = 0;
				parserHave = 0;
				sysexActive = false;
				sysexLength = 0;
				queueMoreAudio();
				return;
			}

			if (data == 0xF0)
			{
				sysexActive = true;
				sysexLength = 0;
				sysexBuffer[sysexLength++] = data;
				parserStatus = 0;
				parserNeeded = 0;
				parserHave = 0;
				queueMoreAudio();
				return;
			}

			if (sysexActive)
			{
				if (sysexLength < (int)(sizeof(sysexBuffer) / sizeof(sysexBuffer[0])))
				{
					sysexBuffer[sysexLength++] = data;
				}
				if (data == 0xF7)
				{
					dispatchSysex();
					sysexActive = false;
					sysexLength = 0;
				}
				queueMoreAudio();
				return;
			}

			if (data >= 0xF8)
			{
				return;
			}

			if (data & 0x80)
			{
				parserStatus = data;
				parserHave = 0;
				switch (parserStatus & 0xF0)
				{
					case 0x80:
					case 0x90:
					case 0xA0:
					case 0xB0:
					case 0xE0:
						parserNeeded = 2;
						break;
					case 0xC0:
					case 0xD0:
						parserNeeded = 1;
						break;
					default:
						parserStatus = 0;
						parserNeeded = 0;
						break;
				}
				queueMoreAudio();
				return;
			}

			if (!(parserStatus && parserNeeded))
			{
				return;
			}

			parserData[parserHave++] = data;
			if (parserHave < parserNeeded)
			{
				queueMoreAudio();
				return;
			}

			dispatchMessage(parserStatus, parserData[0], parserNeeded > 1 ? parserData[1] : 0);
			parserHave = 0;
			queueMoreAudio();
		}

		void reset()
		{
			memset(previousLocalButtons, 0, sizeof(previousLocalButtons));
			memset(localPressed, 0, sizeof(localPressed));
			memset(localSounding, 0, sizeof(localSounding));
			memset(localPhases, 0, sizeof(localPhases));
			memset(localLevels, 0, sizeof(localLevels));
			localSustain = false;
			localInstrument = PreviewPiano;
			localOctaveShift = 0;
			resetRemote();
			parserStatus = 0;
			parserNeeded = 0;
			parserHave = 0;
			sysexActive = false;
			sysexLength = 0;
			vibratoPhase = 0.0;
		}

		void silence()
		{
			reset();
			if (opened)
			{
				SDL_ClearQueuedAudio(device);
			}
		}

		void service() { if (opened) queueMoreAudio(); }
		void stop() { shutdown(); }
		void releaseLocal()
		{
			memset(localPressed, 0, sizeof(localPressed));
			memset(localSounding, 0, sizeof(localSounding));
			memset(localLevels, 0, sizeof(localLevels));
			memset(previousLocalButtons, 0, sizeof(previousLocalButtons));
			localSustain = false;
		}

	private:
		static constexpr int sampleRate = 48000;
		static constexpr int chunkSamples = 512;
		static constexpr Uint32 targetQueueBytes = chunkSamples * sizeof(float) * 6;

		void setLocalState(const FCEUMiraclePianoInputState& state)
		{
			for (int i = 0; i < FCEU_MIRACLE_NUM_BUTTONS; i++)
			{
				const bool pressed = state.pressedButtons[i] != 0;
				if (pressed && !previousLocalButtons[i])
				{
					switch (i)
					{
						case FCEU_MIRACLE_BTN_PIANO: localInstrument = PreviewPiano; break;
						case FCEU_MIRACLE_BTN_HARPSICHORD: localInstrument = PreviewHarpsichord; break;
						case FCEU_MIRACLE_BTN_ORGAN: localInstrument = PreviewOrgan; break;
						case FCEU_MIRACLE_BTN_VIBRAPHONE: localInstrument = PreviewVibraphone; break;
						case FCEU_MIRACLE_BTN_ELECTRIC_PIANO: localInstrument = PreviewElectricPiano; break;
						case FCEU_MIRACLE_BTN_SYNTHESIZER: localInstrument = PreviewSynth; break;
						case FCEU_MIRACLE_BTN_UP:
							if (localOctaveShift < 2) localOctaveShift++;
							break;
						case FCEU_MIRACLE_BTN_DOWN:
							if (localOctaveShift > -2) localOctaveShift--;
							break;
					}
				}
				previousLocalButtons[i] = pressed;
			}

			localSustain = state.sustainPedal != 0;
			for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
			{
				const bool pressed = state.pressedKeys[i] != 0;
				const bool wasPressed = localPressed[i];
				localPressed[i] = pressed;
				if (pressed && !wasPressed)
				{
					localSounding[i] = true;
					localLevels[i] = std::max(0.25, ((state.velocity[i] ? state.velocity[i] : 0x64) / 127.0));
					localPhases[i] = 0.0;
				}
			}

			if (!localSustain)
			{
				for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
				{
					if (!localPressed[i] && isSustainingInstrument(localInstrument))
					{
						localSounding[i] = false;
						localLevels[i] = 0.0;
					}
				}
			}
		}

		void ensureOpen()
		{
			if (opened || !usePreviewDevice)
			{
				return;
			}

			if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0)
			{
				if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
				{
					return;
				}
			}

			SDL_AudioSpec desired;
			SDL_zero(desired);
			desired.freq = sampleRate;
			desired.format = AUDIO_F32SYS;
			desired.channels = 1;
			desired.samples = chunkSamples;

			SDL_zero(obtained);
			device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
			if (device == 0)
			{
				return;
			}

			SDL_PauseAudioDevice(device, 0);
			opened = true;
		}

		void shutdown()
		{
			if (opened)
			{
				SDL_ClearQueuedAudio(device);
				SDL_CloseAudioDevice(device);
				device = 0;
				opened = false;
			}
		}

		void resetRemote()
		{
			memset(remotePressed, 0, sizeof(remotePressed));
			memset(remoteSounding, 0, sizeof(remoteSounding));
			memset(remotePhases, 0, sizeof(remotePhases));
			memset(remoteLevels, 0, sizeof(remoteLevels));
			memset(remoteAttack, 0, sizeof(remoteAttack));
			memset(remoteSustain, 0, sizeof(remoteSustain));
			memset(remoteProgram, 0, sizeof(remoteProgram));
		}

		void silenceRemoteChannel(int channel)
		{
			if (channel < 0 || channel >= 16)
			{
				return;
			}
			memset(remotePressed[channel], 0, sizeof(remotePressed[channel]));
			memset(remoteSounding[channel], 0, sizeof(remoteSounding[channel]));
			memset(remotePhases[channel], 0, sizeof(remotePhases[channel]));
			remoteSustain[channel] = false;
		}

		void releaseRemoteChannel(int channel)
		{
			if (channel < 0 || channel >= 16)
			{
				return;
			}
			memset(remotePressed[channel], 0, sizeof(remotePressed[channel]));
			for (int note = 0; note < 128; note++)
			{
				if (!remoteSustain[channel])
				{
					remoteSounding[channel][note] = false;
				}
			}
		}

		void dispatchSysex()
		{
			if (sysexLength < 7)
			{
				return;
			}
			if (sysexBuffer[0] != 0xF0 || sysexBuffer[1] != 0x00 || sysexBuffer[2] != 0x00 ||
				sysexBuffer[3] != 0x42 || sysexBuffer[4] != 0x01)
			{
				return;
			}
			if (sysexBuffer[5] == 0x07)
			{
				resetRemote();
			}
		}

		int instrumentFromProgram(uint8 program) const
		{
			if (program == 4 || program == 5) return PreviewElectricPiano;
			if (program == 6 || program == 7) return PreviewHarpsichord;
			if (program <= 7) return PreviewPiano;
			if (program >= 16 && program <= 23) return PreviewOrgan;
			if (program == 11) return PreviewVibraphone;
			return PreviewSynth;
		}

		bool isSustainingInstrument(int which) const
		{
			return which == PreviewOrgan || which == PreviewSynth;
		}

		double frequencyForNote(int midiNote) const
		{
			return 440.0 * pow(2.0, (midiNote - 69) / 12.0);
		}

		double keyFrequency(int keyIndex) const
		{
			return frequencyForNote(FCEU_MIRACLE_BASE_NOTE + keyIndex + localOctaveShift * 12);
		}

		double waveform(double phase, int noteValue, int which) const
		{
			const double twoPi = 6.28318530717958647692;
			const double s = sin(twoPi * phase);
			switch (which)
			{
				case PreviewHarpsichord:
					return 0.60 * s + 0.28 * sin(twoPi * 2.0 * phase) + 0.14 * sin(twoPi * 3.0 * phase);
				case PreviewOrgan:
					return 0.72 * s + 0.22 * sin(twoPi * 2.0 * phase) + 0.10 * sin(twoPi * 4.0 * phase);
				case PreviewVibraphone:
				{
					const double lfo = sin(twoPi * vibratoPhase) * 0.015;
					return 0.75 * sin(twoPi * (phase + lfo)) + 0.18 * sin(twoPi * 3.0 * (phase + lfo));
				}
				case PreviewElectricPiano:
					return 0.68 * s + 0.22 * sin(twoPi * 2.0 * phase) + 0.10 * sin(twoPi * 5.0 * phase);
				case PreviewSynth:
					return (s >= 0.0) ? 0.70 : -0.70;
				case PreviewPiano:
				default:
				{
					const double brightness = 0.18 + 0.06 * ((noteValue % 12) / 11.0);
					return 0.78 * s + brightness * sin(twoPi * 2.0 * phase) + 0.08 * sin(twoPi * 3.0 * phase);
				}
			}
		}

		double noteCompensation(int midiNote) const
		{
			const double delta = std::max(0.0, (60.0 - midiNote) / 60.0);
			return 1.0 + delta * 0.35;
		}

		double gameWaveform(double phase, int note, int instrument, double step) const
		{
			if (instrument != PreviewSynth) return waveform(phase, note, instrument);
			const double angle = 6.28318530717958647692 * phase;
			if (step >= 0.5) return 0;
			// Unmapped game instruments need a smooth fallback, not a raw square wave.
			double value = 0.70 * sin(angle);
			if (step * 2.0 < 0.45) value += 0.18 * sin(angle * 2.0);
			if (step * 3.0 < 0.45) value += 0.07 * sin(angle * 3.0);
			return value;
		}

		double decayForInstrument(int which, bool keyHeld) const
		{
			if (isSustainingInstrument(which)) return keyHeld ? 1.0 : 0.99970;
			switch (which)
			{
				case PreviewHarpsichord: return 0.99972;
				case PreviewVibraphone: return 0.99990;
				case PreviewElectricPiano: return 0.99986;
				default: return 0.99984;
			}
		}

		void advanceLocalLevel(double& level, bool& sounding, bool keyHeld) const
		{
			const bool hold = keyHeld || (localSustain && isSustainingInstrument(localInstrument));
			const double decay = decayForInstrument(localInstrument, hold);
			if (hold && isSustainingInstrument(localInstrument))
			{
				if (level < 0.80) level = std::min(0.80, level + 0.0025);
			}
			else
			{
				level *= decay;
			}
			if (level < 0.0005)
			{
				level = 0.0;
				sounding = false;
			}
		}

		void advanceRemoteLevel(double& level, bool& sounding, bool sustainHeld, int which) const
		{
			if (sustainHeld && isSustainingInstrument(which))
			{
				if (level < 0.80) level = std::min(0.80, level + 0.0025);
			}
			else
			{
				switch (which)
				{
					case PreviewHarpsichord: level *= 0.99960; break;
					case PreviewVibraphone: level *= 0.99990; break;
					case PreviewElectricPiano: level *= 0.99982; break;
					case PreviewOrgan:
					case PreviewSynth: level *= 0.99988; break;
					default: level *= 0.99976; break;
				}
			}
			if (level < 0.0005)
			{
				level = 0.0;
				sounding = false;
			}
		}

		void dispatchMessage(uint8 status, uint8 data1, uint8 data2)
		{
			const int channel = status & 0x0F;
			switch (status & 0xF0)
			{
				case 0x80:
					if (data1 < 128) remotePressed[channel][data1] = false;
					break;
				case 0x90:
					if (data1 >= 128) break;
					if (data2)
					{
						remotePressed[channel][data1] = true;
						remoteSounding[channel][data1] = true;
						remoteLevels[channel][data1] = std::max(0.20, data2 / 127.0);
						remotePhases[channel][data1] = 0.0;
						remoteAttack[channel][data1] = 0.0;
					}
					else
					{
						remotePressed[channel][data1] = false;
					}
					break;
				case 0xB0:
					if (data1 == 0x40)
					{
						remoteSustain[channel] = data2 >= 0x40;
					}
					else if (data1 == 0x78 || data1 == 0x7B)
					{
						silenceRemoteChannel(channel);
					}
					else if (data1 == 0x79)
					{
						remoteSustain[channel] = false;
					}
					else if (data1 == 0x7C || data1 == 0x7D || data1 == 0x7E || data1 == 0x7F)
					{
						releaseRemoteChannel(channel);
					}
					break;
				case 0xC0:
					remoteProgram[channel] = instrumentFromProgram(data1);
					break;
			}
		}

		void queueMoreAudio()
		{
			if (!opened)
			{
				return;
			}

			while (SDL_GetQueuedAudioSize(device) < targetQueueBytes)
			{
				std::vector<float> samples(chunkSamples, 0.0f);
				for (int sample = 0; sample < chunkSamples; sample++)
				{
					double mixed = 0.0f;
					int activeCount = 0;

					for (int key = 0; key < FCEU_MIRACLE_NUM_KEYS; key++)
					{
						if (!localSounding[key])
						{
							continue;
						}
						activeCount++;

						const int midiNote = FCEU_MIRACLE_BASE_NOTE + key + localOctaveShift * 12;
						const double step = keyFrequency(key) / obtained.freq;
						localPhases[key] += step;
						if (localPhases[key] >= 1.0)
						{
							localPhases[key] -= floor(localPhases[key]);
						}
						mixed += waveform(localPhases[key], midiNote, localInstrument) * localLevels[key] * noteCompensation(midiNote);
						advanceLocalLevel(localLevels[key], localSounding[key], localPressed[key]);
					}

					for (int channel = 0; channel < 16; channel++)
					{
						for (int note = 0; note < 128; note++)
						{
							if (!remoteSounding[channel][note])
							{
								continue;
							}
							activeCount++;
							const double step = frequencyForNote(note) / obtained.freq;
							remotePhases[channel][note] += step;
							if (remotePhases[channel][note] >= 1.0)
							{
								remotePhases[channel][note] -= floor(remotePhases[channel][note]);
							}
							mixed += waveform(remotePhases[channel][note], note, remoteProgram[channel]) * remoteLevels[channel][note] * noteCompensation(note);
							advanceRemoteLevel(remoteLevels[channel][note], remoteSounding[channel][note], remoteSustain[channel], remoteProgram[channel]);
						}
					}

					vibratoPhase += 5.6 / obtained.freq;
					if (vibratoPhase >= 1.0)
					{
						vibratoPhase -= floor(vibratoPhase);
					}

					if (activeCount > 0)
					{
						const double gain = 0.28 / sqrt((double)activeCount);
						mixed *= gain;
					}
					samples[sample] = (float)std::max(-0.95, std::min(0.95, mixed * outputGain));
				}

				SDL_QueueAudio(device, samples.data(), (Uint32)(samples.size() * sizeof(float)));
			}
		}

		SDL_AudioDeviceID device = 0;
		bool usePreviewDevice;
		SDL_AudioSpec obtained {};
		bool opened = false;
		bool previousLocalButtons[FCEU_MIRACLE_NUM_BUTTONS] {};
		bool localPressed[FCEU_MIRACLE_NUM_KEYS] {};
		bool localSounding[FCEU_MIRACLE_NUM_KEYS] {};
		double localPhases[FCEU_MIRACLE_NUM_KEYS] {};
		double localLevels[FCEU_MIRACLE_NUM_KEYS] {};
		bool localSustain = false;
		int localOctaveShift = 0;
		int localInstrument = PreviewPiano;
		bool remotePressed[16][128] {};
		bool remoteSounding[16][128] {};
		double remotePhases[16][128] {};
		double remoteLevels[16][128] {};
		double remoteAttack[16][128] {};
		bool remoteSustain[16] {};
		int remoteProgram[16] {};
		uint8 parserStatus = 0;
		uint8 parserData[2] {};
		int parserNeeded = 0;
		int parserHave = 0;
		bool sysexActive = false;
		uint8 sysexBuffer[16] {};
		int sysexLength = 0;
		double vibratoPhase = 0.0;
		float outputGain = kMiracleDefaultVolume / 100.0f;
};

static MiraclePreviewSynth g_miraclePreviewSynth;
static MiraclePreviewSynth g_miracleGameSynth(false);
static std::mutex miracleAudioMutex;
static bool miracleAudioInitialized = false;

static void miraclePianoDeviceByteHook(uint8 value)
{
	std::lock_guard<std::mutex> lock(miracleAudioMutex);
	g_miracleGameSynth.pushByte(value);
}

static void miraclePianoResetHook(void)
{
	std::lock_guard<std::mutex> lock(miracleAudioMutex);
	g_miracleGameSynth.reset();
}

}

void initMiraclePianoAudio()
{
	if (miracleAudioInitialized) return;
	QSettings settings;
	setMiraclePianoAudioVolume(settings.value("miraclePianoWindow/previewVolume", kMiracleDefaultVolume).toInt());
	FCEU_SetMiraclePianoOutputHooks(miraclePianoDeviceByteHook, miraclePianoResetHook);
	miracleAudioInitialized = true;
}

void setMiraclePianoAudioVolume(int percent)
{
	g_miraclePreviewSynth.setVolume(percent / 100.0f);
	std::lock_guard<std::mutex> lock(miracleAudioMutex);
	g_miracleGameSynth.setVolume(percent / 100.0f);
}

void mixMiraclePianoAudio(int32* samples, int count, int rate)
{
	// Like bsnes, game-sent piano notes use the emulator's existing sound sink.
	std::lock_guard<std::mutex> lock(miracleAudioMutex);
	g_miracleGameSynth.mixGameAudio(samples, count, rate);
}

void resetMiraclePianoAudio()
{
	std::lock_guard<std::mutex> lock(miracleAudioMutex);
	g_miracleGameSynth.reset();
}

void shutdownMiraclePianoAudio()
{
	miracleAudioInitialized = false;
	FCEU_SetMiraclePianoOutputHooks(nullptr, nullptr);
	resetMiraclePianoAudio();
	g_miraclePreviewSynth.stop();
}

static QPointF mouseEventPosition(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return event->position();
#else
	return event->localPos();
#endif
}

static bool rectIsOnScreen(const QRect& rect)
{
	const QList<QScreen*> screens = QGuiApplication::screens();
	for (QScreen* screen : screens)
	{
		if (screen && screen->availableGeometry().intersects(rect))
		{
			return true;
		}
	}
	return false;
}

MiraclePianoWidget_t::MiraclePianoWidget_t(QWidget* parent)
	: QWidget(parent)
{
	memset(keyPressed, 0, sizeof(keyPressed));
	memset(buttonPressed, 0, sizeof(buttonPressed));
	pedalPressed = false;
	mouseHit = { ElementNone, -1 };
	mousePressed = false;

	setAttribute(Qt::WA_AcceptTouchEvents, true);
	setMouseTracking(true);
	setMinimumSize(900, 420);

	static const char* labels[FCEU_MIRACLE_NUM_BUTTONS] =
	{
		"Piano",
		"Harpsi",
		"Organ",
		"Vibra",
		"E.Piano",
		"Synth",
		"Up",
		"Down"
	};
	for (int i = 0; i < FCEU_MIRACLE_NUM_BUTTONS; i++)
	{
		panelButtons[i].label = QString::fromLatin1(labels[i]);
	}

	updateLayout();
}

void MiraclePianoWidget_t::getState(FCEUMiraclePianoInputState* out) const
{
	if (out == nullptr)
	{
		return;
	}
	memset(out, 0, sizeof(*out));
	for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
	{
		out->pressedKeys[i] = keyPressed[i] ? 1 : 0;
		out->velocity[i] = keyPressed[i] ? 0x64 : 0x00;
	}
	for (int i = 0; i < FCEU_MIRACLE_NUM_BUTTONS; i++)
	{
		out->pressedButtons[i] = buttonPressed[i] ? 1 : 0;
	}
	out->sustainPedal = pedalPressed ? 1 : 0;
}

void MiraclePianoWidget_t::peekState(FCEUMiraclePianoInputState* out) const
{
	getState(out);
}

void MiraclePianoWidget_t::updatePreview(void) const
{
	FCEUMiraclePianoInputState state;
	memset(&state, 0, sizeof(state));
	peekState(&state);
	g_miraclePreviewSynth.update(state);
}

QSize MiraclePianoWidget_t::minimumSizeHint(void) const
{
	return QSize(900, 420);
}

QSize MiraclePianoWidget_t::sizeHint(void) const
{
	return QSize(1120, 520);
}

bool MiraclePianoWidget_t::event(QEvent* event)
{
	if ((event->type() == QEvent::TouchBegin) || (event->type() == QEvent::TouchUpdate) || (event->type() == QEvent::TouchEnd))
	{
		rebuildTouchState(static_cast<QTouchEvent*>(event));
		event->accept();
		return true;
	}
	return QWidget::event(event);
}

void MiraclePianoWidget_t::paintEvent(QPaintEvent* event)
{
	(void)event;
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), QColor(196, 191, 181));

	const QRectF shellRect = rect().adjusted(12.0, 12.0, -12.0, -12.0);
	QLinearGradient shell(0.0, shellRect.top(), 0.0, shellRect.bottom());
	shell.setColorAt(0.0, QColor(224, 219, 209));
	shell.setColorAt(0.42, QColor(206, 199, 188));
	shell.setColorAt(1.0, QColor(180, 172, 162));
	painter.setPen(QPen(QColor(112, 106, 98), 2.0));
	painter.setBrush(shell);
	painter.drawRoundedRect(shellRect, 22.0, 22.0);

	const QRectF panelFace = shellRect.adjusted(18.0, 18.0, -18.0, -shellRect.height() * 0.43);
	QLinearGradient panelGrad(panelFace.left(), panelFace.top(), panelFace.left(), panelFace.bottom());
	panelGrad.setColorAt(0.0, QColor(236, 232, 224));
	panelGrad.setColorAt(1.0, QColor(210, 204, 194));
	painter.setPen(QPen(QColor(130, 122, 112), 1.5));
	painter.setBrush(panelGrad);
	painter.drawRoundedRect(panelFace, 16.0, 16.0);

	QFont labelFont = painter.font();
	labelFont.setBold(true);
	labelFont.setPointSizeF(labelFont.pointSizeF() + 1.0);
	painter.setFont(labelFont);

	painter.setPen(Qt::NoPen);
	for (int i = 0; i < FCEU_MIRACLE_NUM_BUTTONS; i++)
	{
		painter.setPen(QColor(24, 24, 24));
		painter.drawText(panelButtons[i].labelRect, Qt::AlignCenter, panelButtons[i].label);

		const QRectF& buttonRect = panelButtons[i].buttonRect;
		QLinearGradient buttonFill(buttonRect.left(), buttonRect.top(), buttonRect.left(), buttonRect.bottom());
		if (buttonPressed[i])
		{
			buttonFill.setColorAt(0.0, QColor(95, 108, 126));
			buttonFill.setColorAt(1.0, QColor(47, 58, 72));
		}
		else
		{
			buttonFill.setColorAt(0.0, QColor(82, 82, 82));
			buttonFill.setColorAt(1.0, QColor(38, 38, 38));
		}
		painter.setPen(QPen(QColor(8, 8, 8), 1.5));
		painter.setBrush(buttonFill);
		painter.drawRoundedRect(buttonRect, 6.0, 6.0);
		painter.setPen(Qt::NoPen);
	}

	painter.setPen(QColor(22, 22, 22));
	painter.drawText(pedalLabelRect, Qt::AlignCenter, QStringLiteral("Sustain"));

	QLinearGradient pedalFill(pedalRect.left(), pedalRect.top(), pedalRect.left(), pedalRect.bottom());
	if (pedalPressed)
	{
		pedalFill.setColorAt(0.0, QColor(94, 121, 154));
		pedalFill.setColorAt(1.0, QColor(55, 76, 98));
	}
	else
	{
		pedalFill.setColorAt(0.0, QColor(90, 90, 90));
		pedalFill.setColorAt(1.0, QColor(42, 42, 42));
	}
	painter.setPen(QPen(QColor(8, 8, 8), 1.5));
	painter.setBrush(pedalFill);
	painter.drawRoundedRect(pedalRect, 6.0, 6.0);

	painter.setPen(Qt::NoPen);
	for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
	{
		if (isBlackKey(i))
		{
			continue;
		}
		QLinearGradient whiteFill(whiteKeys[i].left(), whiteKeys[i].top(), whiteKeys[i].left(), whiteKeys[i].bottom());
		if (keyPressed[i])
		{
			whiteFill.setColorAt(0.0, QColor(194, 221, 255));
			whiteFill.setColorAt(1.0, QColor(164, 197, 238));
		}
		else
		{
			whiteFill.setColorAt(0.0, QColor(253, 253, 248));
			whiteFill.setColorAt(1.0, QColor(225, 224, 216));
		}
		painter.setBrush(whiteFill);
		painter.setPen(QColor(44, 44, 44));
		painter.drawRect(whiteKeys[i]);
		painter.setPen(Qt::NoPen);
	}

	for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
	{
		if (!isBlackKey(i))
		{
			continue;
		}
		QLinearGradient blackFill(blackKeys[i].left(), blackKeys[i].top(), blackKeys[i].left(), blackKeys[i].bottom());
		if (keyPressed[i])
		{
			blackFill.setColorAt(0.0, QColor(120, 170, 235));
			blackFill.setColorAt(1.0, QColor(52, 74, 108));
		}
		else
		{
			blackFill.setColorAt(0.0, QColor(48, 48, 48));
			blackFill.setColorAt(1.0, QColor(10, 10, 10));
		}
		painter.setBrush(blackFill);
		painter.setPen(QColor(8, 8, 8));
		painter.drawRoundedRect(blackKeys[i], 3.0, 3.0);
	}
}

void MiraclePianoWidget_t::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	updateLayout();
}

void MiraclePianoWidget_t::mousePressEvent(QMouseEvent* event)
{
	const HitResult hit = hitTest(mouseEventPosition(event));
	if (hit.type == ElementPedal)
	{
		togglePedal();
		update();
		updatePreview();
		event->accept();
		return;
	}
	mousePressed = true;
	setMouseHit(hit);
	event->accept();
}

void MiraclePianoWidget_t::mouseMoveEvent(QMouseEvent* event)
{
	if (mousePressed)
	{
		HitResult hit = hitTest(mouseEventPosition(event));
		if (hit.type == ElementPedal)
		{
			hit = { ElementNone, -1 };
		}
		setMouseHit(hit);
	}
}

void MiraclePianoWidget_t::mouseReleaseEvent(QMouseEvent* event)
{
	(void)event;
	mousePressed = false;
	clearMouseHit();
	updatePreview();
}

void MiraclePianoWidget_t::leaveEvent(QEvent* event)
{
	(void)event;
	if (!mousePressed)
	{
		clearMouseHit();
	}
}

bool MiraclePianoWidget_t::isBlackKey(int noteIndex) const
{
	switch (noteIndex % 12)
	{
		case 1:
		case 3:
		case 6:
		case 8:
		case 10:
			return true;
		default:
			return false;
	}
}

int MiraclePianoWidget_t::whiteKeyIndex(int noteIndex) const
{
	int idx = 0;
	for (int i = 0; i < noteIndex; i++)
	{
		if (!isBlackKey(i))
		{
			idx++;
		}
	}
	return idx;
}

void MiraclePianoWidget_t::updateLayout(void)
{
	const QRectF bounds = rect().adjusted(32.0, 28.0, -32.0, -28.0);
	const QRectF panelArea(bounds.left(), bounds.top(), bounds.width(), bounds.height() * 0.34);
	const QRectF pedalArea(bounds.left(), panelArea.bottom() + 6.0, bounds.width(), 74.0);
	const QRectF keysArea(bounds.left(), pedalArea.bottom() + 14.0, bounds.width(), bounds.bottom() - (pedalArea.bottom() + 14.0));

	const int whiteCount = 29;
	const qreal whiteWidth = keysArea.width() / whiteCount;
	const qreal blackWidth = whiteWidth * 0.58;
	const qreal blackHeight = keysArea.height() * 0.62;

	for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
	{
		whiteKeys[i] = QRectF();
		blackKeys[i] = QRectF();
		if (isBlackKey(i))
		{
			const int priorWhite = whiteKeyIndex(i);
			const qreal centerX = keysArea.left() + priorWhite * whiteWidth;
			blackKeys[i] = QRectF(centerX - blackWidth * 0.5, keysArea.top(), blackWidth, blackHeight);
		}
		else
		{
			const int whiteIdx = whiteKeyIndex(i);
			whiteKeys[i] = QRectF(keysArea.left() + whiteIdx * whiteWidth, keysArea.top(), whiteWidth, keysArea.height());
		}
	}

	const qreal buttonGap = 18.0;
	const qreal buttonWidth = qMin((panelArea.width() - buttonGap * (FCEU_MIRACLE_NUM_BUTTONS - 1)) / FCEU_MIRACLE_NUM_BUTTONS, 108.0);
	const qreal totalButtonWidth = buttonWidth * FCEU_MIRACLE_NUM_BUTTONS + buttonGap * (FCEU_MIRACLE_NUM_BUTTONS - 1);
	const qreal buttonStartX = panelArea.left() + (panelArea.width() - totalButtonWidth) * 0.5;
	const qreal labelHeight = 24.0;
	const qreal buttonHeight = qMin(panelArea.height() - 42.0, buttonWidth);
	for (int i = 0; i < FCEU_MIRACLE_NUM_BUTTONS; i++)
	{
		const qreal x = buttonStartX + i * (buttonWidth + buttonGap);
		panelButtons[i].labelRect = QRectF(x - 6.0, panelArea.top() + 4.0, buttonWidth + 12.0, labelHeight);
		panelButtons[i].buttonRect = QRectF(x, panelArea.top() + labelHeight + 10.0, buttonWidth, buttonHeight);
	}

	pedalLabelRect = QRectF(bounds.center().x() - 76.0, pedalArea.top(), 152.0, 22.0);
	pedalRect = QRectF(bounds.center().x() - 44.0, pedalArea.top() + 28.0, 88.0, 32.0);
	update();
}

MiraclePianoWidget_t::HitResult MiraclePianoWidget_t::hitTest(const QPointF& point) const
{
	for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
	{
		if (isBlackKey(i) && blackKeys[i].contains(point))
		{
			return { ElementBlackKey, i };
		}
	}
	for (int i = 0; i < FCEU_MIRACLE_NUM_BUTTONS; i++)
	{
		if (panelButtons[i].buttonRect.contains(point))
		{
			return { ElementButton, i };
		}
	}
	if (pedalRect.contains(point))
	{
		return { ElementPedal, 0 };
	}
	for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
	{
		if ((!isBlackKey(i)) && whiteKeys[i].contains(point))
		{
			return { ElementWhiteKey, i };
		}
	}
	return { ElementNone, -1 };
}

void MiraclePianoWidget_t::applyHitState(const HitResult& hit, bool pressed)
{
	switch (hit.type)
	{
		case ElementWhiteKey:
		case ElementBlackKey:
			if ((hit.index >= 0) && (hit.index < FCEU_MIRACLE_NUM_KEYS))
			{
				keyPressed[hit.index] = pressed;
			}
			break;
		case ElementButton:
			if ((hit.index >= 0) && (hit.index < FCEU_MIRACLE_NUM_BUTTONS))
			{
				buttonPressed[hit.index] = pressed;
			}
			break;
		case ElementPedal:
			break;
		default:
			break;
	}
}

void MiraclePianoWidget_t::setMouseHit(const HitResult& hit)
{
	if ((mouseHit.type == hit.type) && (mouseHit.index == hit.index))
	{
		return;
	}
	applyHitState(mouseHit, false);
	mouseHit = hit;
	applyHitState(mouseHit, true);
	update();
	updatePreview();
}

void MiraclePianoWidget_t::clearMouseHit(void)
{
	applyHitState(mouseHit, false);
	mouseHit = { ElementNone, -1 };
	update();
	updatePreview();
}

void MiraclePianoWidget_t::rebuildTouchState(QTouchEvent* event)
{
	const QHash<int, HitResult> previousHits = touchHits;
	memset(keyPressed, 0, sizeof(keyPressed));
	memset(buttonPressed, 0, sizeof(buttonPressed));

	if (mousePressed)
	{
		applyHitState(mouseHit, true);
	}

	touchHits.clear();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	for (const auto& point : event->points())
	{
		if (point.state() == QEventPoint::State::Released)
		{
			continue;
		}
		const HitResult hit = hitTest(point.position());
		if (hit.type != ElementNone)
		{
			touchHits.insert(point.id(), hit);
			if (hit.type == ElementPedal)
			{
				if (!previousHits.contains(point.id()) || (previousHits.value(point.id()).type != ElementPedal))
				{
					togglePedal();
				}
			}
			else
			{
				applyHitState(hit, true);
			}
		}
	}
#else
	for (const QTouchEvent::TouchPoint& point : event->touchPoints())
	{
		if (point.state() == Qt::TouchPointReleased)
		{
			continue;
		}
		const HitResult hit = hitTest(point.pos());
		if (hit.type != ElementNone)
		{
			touchHits.insert(point.id(), hit);
			if (hit.type == ElementPedal)
			{
				if (!previousHits.contains(point.id()) || (previousHits.value(point.id()).type != ElementPedal))
				{
					togglePedal();
				}
			}
			else
			{
				applyHitState(hit, true);
			}
		}
	}
#endif
	update();
	updatePreview();
}

void MiraclePianoWidget_t::togglePedal(void)
{
	pedalPressed = !pedalPressed;
	updatePreview();
}

MiraclePianoDialog_t::MiraclePianoDialog_t(QWidget* parent)
	: QDialog(parent)
{
	QVBoxLayout* layout = new QVBoxLayout();
	layout->setContentsMargins(8, 8, 8, 8);

	setWindowTitle(QStringLiteral("Miracle Piano"));
	setWindowFlag(Qt::WindowMaximizeButtonHint, true);
	setSizeGripEnabled(true);
	setLayout(layout);

	QHBoxLayout* volumeLayout = new QHBoxLayout();
	QLabel* volumeLabel = new QLabel(QStringLiteral("Keyboard Volume:"), this);
	volumeSlider = new QSlider(Qt::Horizontal, this);
	volumeValue = new QLabel(this);
	volumeSlider->setRange(25, 400);
	layout->addLayout(volumeLayout);
	volumeLayout->addStretch(1);
	volumeLayout->addWidget(volumeLabel);
	volumeLayout->addWidget(volumeSlider, 1);
	volumeLayout->addWidget(volumeValue);
	volumeLayout->addStretch(1);

	piano = new MiraclePianoWidget_t(this);
	layout->addWidget(piano);
	piano->installEventFilter(this);

	updateTimer = new QTimer(this);
	connect(updateTimer, &QTimer::timeout, this, &MiraclePianoDialog_t::periodicUpdate);
	updateTimer->start(16);

	focusReturnTarget = parent;
	focusReturnTimer = new QTimer(this);
	focusReturnTimer->setSingleShot(true);
	connect(focusReturnTimer, &QTimer::timeout, this, &MiraclePianoDialog_t::returnFocusToGame);

	QSettings settings;
	volumeSlider->setValue(settings.value(QStringLiteral("miraclePianoWindow/previewVolume"), kMiracleDefaultVolume).toInt());
	setMiraclePianoAudioVolume(volumeSlider->value());
	updateVolumeLabel();
	connect(volumeSlider, &QSlider::valueChanged, this, [this](int value) {
		setMiraclePianoAudioVolume(value);
		updateVolumeLabel();
	});
	resize(settings.value(QStringLiteral("miraclePianoWindow/size"), QSize(1120, 420)).toSize());
	const QRect savedRect(settings.value(QStringLiteral("miraclePianoWindow/geometry")).toRect());
	if (savedRect.isValid() && rectIsOnScreen(savedRect))
	{
		setGeometry(savedRect);
	}
	if (settings.value(QStringLiteral("miraclePianoWindow/maximized"), false).toBool())
	{
		QTimer::singleShot(0, this, [this]() { showMaximized(); });
	}

	QShortcut* toggleShortcut = new QShortcut(QKeySequence(QStringLiteral("M")), this);
	connect(toggleShortcut, &QShortcut::activated, this, &QDialog::close);
}

MiraclePianoDialog_t::~MiraclePianoDialog_t(void)
{
}

void MiraclePianoDialog_t::getState(FCEUMiraclePianoInputState* out) const
{
	piano->getState(out);
}

bool MiraclePianoDialog_t::eventFilter(QObject* watched, QEvent* event)
{
	if ((watched == piano) && (event != nullptr))
	{
		switch (event->type())
		{
			case QEvent::MouseButtonPress:
			case QEvent::MouseMove:
			case QEvent::MouseButtonRelease:
			case QEvent::TouchBegin:
			case QEvent::TouchUpdate:
			case QEvent::TouchEnd:
			case QEvent::KeyPress:
			case QEvent::KeyRelease:
				scheduleFocusReturn();
				break;
			default:
				break;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void MiraclePianoDialog_t::closeEvent(QCloseEvent* event)
{
	g_miraclePreviewSynth.releaseLocal();

	QSettings settings;
	settings.setValue(QStringLiteral("miraclePianoWindow/size"), size());
	settings.setValue(QStringLiteral("miraclePianoWindow/geometry"), isMaximized() ? normalGeometry() : geometry());
	settings.setValue(QStringLiteral("miraclePianoWindow/maximized"), isMaximized());
	settings.setValue(QStringLiteral("miraclePianoWindow/previewVolume"), volumeSlider != nullptr ? volumeSlider->value() : kMiracleDefaultVolume);

	if (miracleWindow == this)
	{
		miracleWindow = nullptr;
	}
	QDialog::closeEvent(event);
	deleteLater();
}

void MiraclePianoDialog_t::periodicUpdate(void)
{
	FCEUMiraclePianoInputState state;
	memset(&state, 0, sizeof(state));
	piano->peekState(&state);
	g_miraclePreviewSynth.update(state);
}

void MiraclePianoDialog_t::scheduleFocusReturn(void)
{
	if (focusReturnTimer != nullptr)
	{
		focusReturnTimer->start(1500);
	}
}

void MiraclePianoDialog_t::returnFocusToGame(void)
{
	if ((focusReturnTarget != nullptr) && focusReturnTarget->isVisible())
	{
		focusReturnTarget->activateWindow();
		focusReturnTarget->raise();
		focusReturnTarget->setFocus(Qt::OtherFocusReason);
	}
}

void MiraclePianoDialog_t::updateVolumeLabel(void)
{
	if (volumeValue != nullptr && volumeSlider != nullptr)
	{
		volumeValue->setText(QStringLiteral("%1%").arg(volumeSlider->value()));
	}
}

int openMiraclePianoWindow(QWidget* parent)
{
	initMiraclePianoAudio();
	if (miracleWindow != nullptr)
	{
		miracleWindow->show();
		miracleWindow->raise();
		miracleWindow->activateWindow();
		return 0;
	}

	miracleWindow = new MiraclePianoDialog_t(parent);
	miracleWindow->show();
	miracleWindow->raise();
	return 0;
}

int toggleMiraclePianoWindow(QWidget* parent)
{
	if (miracleWindow != nullptr)
	{
		closeMiraclePianoWindow();
		return 0;
	}
	return openMiraclePianoWindow(parent);
}

bool miraclePianoWindowIsOpen(void)
{
	return miracleWindow != nullptr;
}

void closeMiraclePianoWindow(void)
{
	if (miracleWindow != nullptr)
	{
		miracleWindow->close();
	}
}

void getMiraclePianoVirtualState(FCEUMiraclePianoInputState* out)
{
	if (out == nullptr)
	{
		return;
	}
	memset(out, 0, sizeof(*out));
	if (miracleWindow != nullptr)
	{
		miracleWindow->getState(out);
	}
}
