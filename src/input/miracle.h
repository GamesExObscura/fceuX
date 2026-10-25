#pragma once

#include "../types.h"
#include "../input.h"

enum
{
	FCEU_MIRACLE_NUM_KEYS = 49,
	FCEU_MIRACLE_NUM_BUTTONS = 8,
	FCEU_MIRACLE_BASE_NOTE = 0x24,
	FCEU_MIRACLE_BASE_BUTTON_NOTE = 0x58
};

enum FCEUMiracleButton
{
	FCEU_MIRACLE_BTN_PIANO = 0,
	FCEU_MIRACLE_BTN_HARPSICHORD,
	FCEU_MIRACLE_BTN_ORGAN,
	FCEU_MIRACLE_BTN_VIBRAPHONE,
	FCEU_MIRACLE_BTN_ELECTRIC_PIANO,
	FCEU_MIRACLE_BTN_SYNTHESIZER,
	FCEU_MIRACLE_BTN_UP,
	FCEU_MIRACLE_BTN_DOWN
};

struct FCEUMiraclePianoInputState
{
	uint8 pressedKeys[FCEU_MIRACLE_NUM_KEYS];
	uint8 velocity[FCEU_MIRACLE_NUM_KEYS];
	uint8 pressedButtons[FCEU_MIRACLE_NUM_BUTTONS];
	uint8 sustainPedal;
};

typedef void (*FCEUMiracleOutputByteHook)(uint8 value);
typedef void (*FCEUMiracleOutputResetHook)(void);

INPUTC* FCEU_InitMiraclePiano(int w);
void FCEU_SetMiraclePianoOutputHooks(FCEUMiracleOutputByteHook byteHook, FCEUMiracleOutputResetHook resetHook);
