/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2026 OpenAI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>

#include "share.h"
#include "miracle.h"

static FCEUMiraclePianoInputState curState;
static FCEUMiraclePianoInputState prevState;

static uint8 queuedBytes[512];
static int queueHead = 0;
static int queueSize = 0;

static uint8 currentOutBit = 0;
static uint64 highStartTs = 0;
static uint8 shortPollPending = 0;
static uint8 captureActive = 0;
static uint8 captureByte = 0;
static uint8 captureBits = 0;

static uint8 replyActive = 0;
static uint8 replyByte = 0;
static uint8 replyBit = 0;

static uint8 sysexActive = 0;
static uint8 sysexBuffer[16];
static uint8 sysexLength = 0;
static uint8 bootstrapIdentityPending = 0;
static FCEUMiracleOutputByteHook outputByteHook = nullptr;
static FCEUMiracleOutputResetHook outputResetHook = nullptr;

static uint64 MiracleNow(void)
{
	return timestampbase + timestamp;
}

static void QueueByte(uint8 value)
{
	if (queueSize >= (int)ARRAY_SIZE(queuedBytes))
	{
		return;
	}
	queuedBytes[(queueHead + queueSize) % (int)ARRAY_SIZE(queuedBytes)] = value;
	queueSize++;
}

static uint8 PeekByte(void)
{
	return (queueSize > 0) ? queuedBytes[queueHead] : 0;
}

static void PopByte(void)
{
	if (queueSize <= 0)
	{
		return;
	}
	queueHead = (queueHead + 1) % (int)ARRAY_SIZE(queuedBytes);
	queueSize--;
}

static void QueueBytes(const uint8* data, int length)
{
	for (int i = 0; i < length; i++)
	{
		QueueByte(data[i]);
	}
}

static void QueueVersionResponse(void)
{
	// Match the commonly documented Miracle Piano identity reply so the ROM
	// recognizes the device during startup probing.
	static const uint8 response[] = { 0xF0, 0x00, 0x00, 0x42, 0x01, 0x05, 0x01, 0x0A, 0xF7 };
	QueueBytes(response, (int)ARRAY_SIZE(response));
}

static void QueueMemoryTestResponse(void)
{
	static const uint8 response[] = { 0xF0, 0x00, 0x00, 0x42, 0x01, 0x0B, 0x01, 0xF7 };
	QueueBytes(response, (int)ARRAY_SIZE(response));
}

static void ResetParser(void)
{
	sysexActive = 0;
	sysexLength = 0;
}

static void HandleSysex(void)
{
	if (sysexLength < 6)
	{
		return;
	}
	if ((sysexBuffer[0] != 0xF0) || (sysexBuffer[1] != 0x00) || (sysexBuffer[2] != 0x00) ||
		(sysexBuffer[3] != 0x42) || (sysexBuffer[4] != 0x01))
	{
		return;
	}

	switch (sysexBuffer[5])
	{
		case 0x04:
			QueueVersionResponse();
			ResetParser();
			break;
		case 0x07:
			queueHead = 0;
			queueSize = 0;
			replyActive = 0;
			replyBit = 0;
			bootstrapIdentityPending = 1;
			if (outputResetHook != nullptr)
			{
				outputResetHook();
			}
			ResetParser();
			break;
		case 0x0B:
			QueueMemoryTestResponse();
			ResetParser();
			break;
		default:
			if (sysexLength >= (int)ARRAY_SIZE(sysexBuffer))
			{
				ResetParser();
			}
			break;
	}
}

static void ProcessIncomingByte(uint8 value)
{
	if (outputByteHook != nullptr)
	{
		outputByteHook(value);
	}

	if (value == 0xF7)
	{
		ResetParser();
		return;
	}

	if (value == 0xF0)
	{
		sysexActive = 1;
		sysexLength = 0;
	}

	if (sysexActive)
	{
		if (sysexLength < (int)ARRAY_SIZE(sysexBuffer))
		{
			sysexBuffer[sysexLength++] = value;
			HandleSysex();
		}
		else
		{
			ResetParser();
		}
	}
}

static void QueueNoteEvent(uint8 note, uint8 velocity, bool pressed)
{
	QueueByte(pressed ? 0x90 : 0x80);
	QueueByte(note);
	QueueByte(pressed ? velocity : 0x00);
}

static void QueueSustainEvent(bool pressed)
{
	QueueByte(0xB0);
	QueueByte(0x40);
	QueueByte(pressed ? 0x7F : 0x00);
}

static void UpdateMiracleInput(int w, void* data, int arg)
{
	(void)w;
	(void)arg;
	if (data != nullptr)
	{
		memcpy(&curState, data, sizeof(curState));
	}

	for (int i = 0; i < FCEU_MIRACLE_NUM_KEYS; i++)
	{
		const bool pressed = curState.pressedKeys[i] != 0;
		const bool wasPressed = prevState.pressedKeys[i] != 0;
		if (pressed != wasPressed)
		{
			const uint8 velocity = curState.velocity[i] ? curState.velocity[i] : 0x64;
			QueueNoteEvent((uint8)(FCEU_MIRACLE_BASE_NOTE + i), velocity, pressed);
		}
	}

	for (int i = 0; i < FCEU_MIRACLE_NUM_BUTTONS; i++)
	{
		const bool pressed = curState.pressedButtons[i] != 0;
		const bool wasPressed = prevState.pressedButtons[i] != 0;
		if (pressed != wasPressed)
		{
			QueueNoteEvent((uint8)(FCEU_MIRACLE_BASE_BUTTON_NOTE + i), 0x64, pressed);
		}
	}

	if ((curState.sustainPedal != 0) != (prevState.sustainPedal != 0))
	{
		QueueSustainEvent(curState.sustainPedal != 0);
	}

	prevState = curState;
}

static void WriteMiracle(uint8 value)
{
	const uint64 now = MiracleNow();
	const uint8 nextBit = value & 1;

	// The NES sends a long high preamble, then eight MSB-first writes.
	// Reads clock the opposite direction (keyboard replies), not these bits.
	if (!captureActive && currentOutBit && (now - highStartTs >= 50))
	{
		captureActive = 1;
		captureByte = 0;
		captureBits = 0;
		shortPollPending = 0;
	}
	if (captureActive)
	{
		currentOutBit = nextBit;
		highStartTs = now;
		captureByte = (uint8)((captureByte << 1) | nextBit);
		if (++captureBits == 8)
		{
			captureActive = 0;
			captureBits = 0;
			ProcessIncomingByte(captureByte);
		}
		return;
	}

	if ((!currentOutBit) && nextBit)
	{
		highStartTs = now;
	}
	else if (currentOutBit && (!nextBit))
	{
		const uint64 pulseLen = now - highStartTs;
		if (pulseLen >= 6 && pulseLen <= 40)
		{
			shortPollPending = 1;
		}
	}

	currentOutBit = nextBit;
}

static void StrobeMiracle(int w)
{
	(void)w;
}

static uint8 ReadMiracle(int w)
{
	(void)w;

	if (replyActive)
	{
		const uint8 bit = (uint8)(((~replyByte) >> (7 - replyBit)) & 1);
		replyBit++;
		if (replyBit >= 8)
		{
			replyActive = 0;
			replyBit = 0;
			PopByte();
		}
		return bit;
	}

	if (!shortPollPending)
	{
		return 0;
	}
	shortPollPending = 0;

	if (!replyActive)
	{
		if ((queueSize <= 0) && bootstrapIdentityPending)
		{
			QueueVersionResponse();
			bootstrapIdentityPending = 0;
		}
		if (queueSize <= 0)
		{
			return 0;
		}
		replyActive = 1;
		replyByte = PeekByte();
		replyBit = 0;
		return 1;
	}

	return 0;
}

static INPUTC MiraclePiano = { ReadMiracle, WriteMiracle, StrobeMiracle, UpdateMiracleInput, 0, 0, 0, 0 };

void FCEU_SetMiraclePianoOutputHooks(FCEUMiracleOutputByteHook byteHook, FCEUMiracleOutputResetHook resetHook)
{
	outputByteHook = byteHook;
	outputResetHook = resetHook;
}

INPUTC* FCEU_InitMiraclePiano(int w)
{
	(void)w;
	memset(&curState, 0, sizeof(curState));
	memset(&prevState, 0, sizeof(prevState));
	queueHead = 0;
	queueSize = 0;
	currentOutBit = 0;
	highStartTs = 0;
	shortPollPending = 0;
	captureActive = 0;
	captureByte = 0;
	captureBits = 0;
	replyActive = 0;
	replyByte = 0;
	replyBit = 0;
	bootstrapIdentityPending = 1;
	ResetParser();
	if (outputResetHook != nullptr)
	{
		outputResetHook();
	}
	return &MiraclePiano;
}
