# Miracle game audio update - September 11, 2026

## NES receiver correction

The first update fixed audio lifetime but missed a serial protocol bug. A trace
of the actual NES ROM showed a 66-cycle high preamble followed by eight
MSB-first writes, 15 cycles apart, and a trailing low write. The old receiver
incorrectly sampled outgoing data on reads. It now captures on writes; reads
remain reserved for keyboard-to-game replies. Actual opening-music traffic now
decodes as instrument changes, note-ons and note-offs instead of bogus zero
bytes. Temporary port tracing was removed from the release build.

The regression now uses the observed write-only sequence (not the previous
synthetic sequence that also issued reads), and checks keyboard identity and
note replies in the other direction. The user confirmed audio playback and
accepted the final distortion correction before promotion to the main package.

Game-to-piano output is attached before ROM initialization. Like bsnes, music
is mixed into the emulator's normal audio output, independently of the viewer.
The game synth is protected by a mutex, and does not open a second SDL device.
Manually played keys retain their separate preview sound. Closing the viewer
releases local keys without stopping game playback. ROM close/reset clears
game playback, and application shutdown closes preview audio before SDL quits.

Game-selected fallback instruments now use smooth harmonic waveforms rather
than raw square waves, with a short note attack. Chord levels use an amplitude
budget and linear headroom adjustment instead of saturation. Manual-key volume
and instrument sounds are unchanged.

Select Miracle Piano as the game's input device as before. The piano window's
Keyboard Volume slider controls both local keys and game-sent piano music, with
the existing maximum of 400%. No controller configuration is overwritten.

## Verification

The isolated regression check exercises the production NES serial receiver and
audio service, using a recording SDL stub instead of a physical audio device.
It checks game samples without a second device, keyboard-to-game input, local
sound, reset, all-sound-off, shutdown, 400% gain, chord headroom and fallback
waveform continuity. The final 16-note chord test peaked at 19622/32767 at 400%.
Actual output-callback audio was captured during development. Runtime tests
were stopped after the user reported memory-error dialogs; the final changes
were checked offline and then tested and accepted by the user. Those dialogs
were not independently diagnosed.

Run in PowerShell with the installed MSVC and Qt/SDL SDKs:

```powershell
& ./tests/run-miracle-audio-check.ps1
```

The runner accepts `-Qt`, `-Sdl`, and `-Build` paths. It changes only its process
PATH and restores it afterward. No user/system PATH changes are needed.

The updated executable is built in `../build-miracle-nmake/src/fceux.exe` from
this source tree using Release MSVC, Qt 6.11.0, and SDL 2.30.12. The older
`FCEUX GEO` directory is not this update's build directory.
