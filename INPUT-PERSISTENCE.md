# Input persistence - September 11, 2026

Manual selections in Input Config now update both the live device and saved
preferences. Closing/loading a ROM retains them; ROM controller metadata no
longer replaces them. Selections also survive application restarts. Existing
named configuration values remain supported; newly saved device choices use
validated numeric device IDs in the existing SDL.Input.0/1/2 options.

The GUI holds the emulator lock while changing device interfaces. Unchanged
ports are not reinitialized when another port changes. Attaching Miracle Piano
or a Zapper initializes its input data; detaching the piano stops game-driven
piano notes. Movie playback's temporary device choices do not overwrite the
user's saved preferences.

Explicit Auto Load/Save Presets remains opt-in and may load a different saved
per-ROM setup. Leave it off to use the last manual choice across all games.
Device changes apply without resetting the ROM, but software that has already
given up probing for a peripheral may still require a game reset. Four Score
continues to require gamepads on its ports.

Build and offline tests passed (tests/run-input-selection-check.ps1 and
tests/run-miracle-audio-check.ps1). No FCEUX runtime was launched for this update;
live-game switching remains a user-side verification step.
