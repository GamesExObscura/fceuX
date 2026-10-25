#include <algorithm>
#include <cmath>
#include <cstring>

#include "share.h"
#include "../fceu.h"
#include "../input.h"
#include "../movie.h"
#include "../state.h"
#include "../video.h"

extern uint8* RAM;
extern uint32 iNESGameCRC32;
extern uint8 joy[4];

namespace {

constexpr uint32 ROB_GYROMITE_CRC0 = 0x023A5A32;
constexpr uint32 ROB_GYROMITE_CRC1 = 0x84EF2FF9;
constexpr uint32 ROB_STACKUP_CRC0  = 0xDF67DAA1;
constexpr uint32 ROB_STACKUP_CRC1  = 0x97B0F110;
constexpr int ROB_GYROMITE_GYRO_COUNT = 2;
constexpr int ROB_GYRO_TIMEOUT_FRAMES = 60 * 60;

constexpr int ROB_NONE = 0;
constexpr int ROB_GYROMITE = 1;
constexpr int ROB_STACK_UP = 2;

enum RobCommand {
	ROB_CMD_NONE = 0,
	ROB_CMD_MOVE_UP,
	ROB_CMD_MOVE_DOWN,
	ROB_CMD_MOVE_LEFT,
	ROB_CMD_MOVE_RIGHT,
	ROB_CMD_OPEN_HANDS,
	ROB_CMD_CLOSE_HANDS,
	ROB_CMD_TEST,
	ROB_CMD_REPAINT,
	ROB_CMD_ADVANCE_PHASE,
};

struct RobStateData {
	float pieces[5][2];
	int32 modifications;
	int32 game;
	float handsX;
	float handsY;
	float handsOpen;
	float aButtonY;
	float bButtonY;
	uint8 testing;
};

struct RobRuntime {
	RobStateData state;
	int32 executingCommand;
	int32 counter;
	int32 shiftRegister;
	int32 requestedCommand;
	int32 heldGyro;
	float fallingVelocity;
	uint8 heldBlocks[5];
	int32 puzzles[4][6][5];
	float stackHeight;
	int32 gyroTimers[2];
	uint8 gyroSpinnerState[2];
};

static RobRuntime rob;

inline float rob_max(const float a, const float b)
{
	return std::max(a, b);
}

inline float rob_min(const float a, const float b)
{
	return std::min(a, b);
}

inline float rob_round(const float value)
{
	return std::round(value);
}

inline uint8 read_ram(const int address)
{
	return RAM[address & 0x7FF];
}

float GyromiteHomeX(const int index)
{
	return -2.0f + static_cast<float>(index);
}

void RobInitTest();

void RobClearRuntime()
{
	std::memset(&rob, 0, sizeof(rob));
	rob.executingCommand = ROB_CMD_NONE;
	rob.requestedCommand = ROB_CMD_NONE;
	rob.heldGyro = -1;
}

void RobResetState()
{
	rob.state.modifications = 0;
	rob.state.handsOpen = 1.0f;
	rob.state.handsX = 0.0f;
	rob.state.handsY = 0.0f;
	rob.state.testing = 0;
	rob.state.aButtonY = 0.0f;
	rob.state.bButtonY = 0.0f;
	std::memset(rob.state.pieces, 0, sizeof(rob.state.pieces));
	std::memset(rob.heldBlocks, 0, sizeof(rob.heldBlocks));
	std::memset(rob.puzzles, 0, sizeof(rob.puzzles));
	rob.executingCommand = ROB_CMD_NONE;
	rob.requestedCommand = ROB_CMD_NONE;
	rob.counter = 0;
	rob.shiftRegister = 0;
	rob.heldGyro = -1;
	rob.fallingVelocity = 0.0f;
	rob.stackHeight = 0.0f;
	std::memset(rob.gyroTimers, 0, sizeof(rob.gyroTimers));
	std::memset(rob.gyroSpinnerState, 0, sizeof(rob.gyroSpinnerState));

	if (rob.state.game == ROB_GYROMITE) {
		rob.state.aButtonY = 3.0f;
		rob.state.bButtonY = 3.0f;

		rob.state.pieces[0][0] = -2.0f;
		rob.state.pieces[0][1] = 4.0f;
		rob.state.pieces[1][0] = -1.0f;
		rob.state.pieces[1][1] = 4.0f;
		rob.state.pieces[2][0] = 8.0f;
		rob.state.pieces[2][1] = 8.0f;
	} else if (rob.state.game == ROB_STACK_UP) {
		for (int i = 0; i < 5; ++i) {
			rob.state.pieces[i][0] = 0.0f;
			rob.state.pieces[i][1] = static_cast<float>(i + 1);
		}
	}
}

int RobDetectGame()
{
	switch (iNESGameCRC32) {
	case ROB_GYROMITE_CRC0:
	case ROB_GYROMITE_CRC1:
		return ROB_GYROMITE;
	case ROB_STACKUP_CRC0:
	case ROB_STACKUP_CRC1:
		return ROB_STACK_UP;
	default:
		return ROB_NONE;
	}
}

void RobInitForGame()
{
	RobClearRuntime();
	rob.state.game = RobDetectGame();
	RobResetState();
}

void RobRequest(const int command)
{
	if (rob.state.game == ROB_GYROMITE && command == ROB_CMD_TEST) {
		rob.requestedCommand = ROB_CMD_NONE;
		rob.executingCommand = ROB_CMD_TEST;
		RobInitTest();
		return;
	}
	rob.requestedCommand = command;
}

void RobSignal(const bool greenScreen)
{
	rob.shiftRegister = (rob.shiftRegister << 1) | (greenScreen ? 1 : 0);
	switch (rob.shiftRegister & 0x1FFF) {
	case 0x02AE:
	case 0x02FB:
		RobRequest(ROB_CMD_MOVE_DOWN);
		break;
	case 0x02BA:
		RobRequest(ROB_CMD_MOVE_LEFT);
		break;
	case 0x02BB:
	case 0x02FA:
		RobRequest(ROB_CMD_MOVE_UP);
		break;
	case 0x02BE:
		RobRequest(ROB_CMD_CLOSE_HANDS);
		break;
	case 0x02EA:
		RobRequest(ROB_CMD_MOVE_RIGHT);
		break;
	case 0x02EB:
		RobRequest(ROB_CMD_TEST);
		break;
	case 0x02EE:
		RobRequest(ROB_CMD_OPEN_HANDS);
		break;
	}
}

int GyromiteGetUnheldGyro(const float x)
{
	for (int i = ROB_GYROMITE_GYRO_COUNT - 1; i >= 0; --i) {
		if (i != rob.heldGyro && rob.state.pieces[i][0] == x) {
			return i;
		}
	}
	return -1;
}

bool GyromiteContainsUnheldGyro(const float x)
{
	return GyromiteGetUnheldGyro(x) >= 0;
}

bool GyromiteGyroOnSpinner(const int index)
{
	return index >= 0
		&& index < ROB_GYROMITE_GYRO_COUNT
		&& rob.heldGyro != index
		&& rob.state.pieces[index][0] == 2.0f
		&& rob.state.pieces[index][1] == 2.0f;
}

bool GyromiteGyroTimerStarted(const int index)
{
	return index >= 0
		&& index < ROB_GYROMITE_GYRO_COUNT
		&& (rob.gyroTimers[index] > 0 || rob.gyroSpinnerState[index] != 0);
}

void GyromiteResetHands()
{
	rob.executingCommand = ROB_CMD_NONE;
	rob.requestedCommand = ROB_CMD_NONE;
	rob.counter = 0;
	rob.heldGyro = -1;
	rob.fallingVelocity = 0.0f;
	rob.state.testing = 0;
	rob.state.handsOpen = 1.0f;
	rob.state.handsX = 0.0f;
	rob.state.handsY = 0.0f;
}

void GyromiteRefreshButtons()
{
	rob.state.aButtonY = 3.0f;
	rob.state.bButtonY = 3.0f;

	for (int i = 0; i < ROB_GYROMITE_GYRO_COUNT; ++i) {
		if (i == rob.heldGyro) {
			continue;
		}

		if (rob.state.pieces[i][0] == 0.0f) {
			rob.state.aButtonY = rob_max(rob.state.aButtonY,
				rob_min(4.0f, rob.state.pieces[i][1]));
		} else if (rob.state.pieces[i][0] == 1.0f) {
			rob.state.bButtonY = rob_max(rob.state.bButtonY,
				rob_min(4.0f, rob.state.pieces[i][1]));
		}
	}
}

void GyromiteResetGyro(const int index)
{
	if (rob.heldGyro == index) {
		GyromiteResetHands();
	}
	rob.state.pieces[index][0] = GyromiteHomeX(index);
	rob.state.pieces[index][1] = 4.0f;
	rob.gyroTimers[index] = 0;
	rob.gyroSpinnerState[index] = 0;
	GyromiteRefreshButtons();
	++rob.state.modifications;
}

void GyromiteUpdateGyroTimers()
{
	for (int i = 0; i < ROB_GYROMITE_GYRO_COUNT; ++i) {
		const bool onSpinner = GyromiteGyroOnSpinner(i);
		if (onSpinner) {
			if (!rob.gyroSpinnerState[i]) {
				rob.gyroTimers[i] = ROB_GYRO_TIMEOUT_FRAMES;
			}
		}

		if (rob.gyroTimers[i] > 0) {
			if (!onSpinner || rob.gyroSpinnerState[i]) {
				--rob.gyroTimers[i];
			}
			if (rob.gyroTimers[i] <= 0) {
				GyromiteResetGyro(i);
				continue;
			}
		}
		rob.gyroSpinnerState[i] = onSpinner ? 1 : 0;
	}
}

void RobInitTest()
{
	if (rob.state.game == ROB_GYROMITE) {
		RobResetState();
	}
	rob.state.testing = 1;
	rob.counter = 60;
}

void RobInitRepaint()
{
	rob.counter = 2;
}

void RobInitAdvancePhase()
{
	rob.executingCommand = ROB_CMD_NONE;
}

void RobExecuteTest()
{
	if (--rob.counter == 0) {
		rob.state.testing = 0;
		rob.executingCommand = ROB_CMD_NONE;
	}
}

void RobExecuteRepaint()
{
	if (--rob.counter == 0) {
		rob.executingCommand = ROB_CMD_NONE;
	}
}

float StackUpGetStackHeight(const float x)
{
	float height = 5.0f;
	for (int i = 4; i >= 0; --i) {
		if (!rob.heldBlocks[i] && rob.state.pieces[i][0] == x) {
			height = rob_min(height, rob.state.pieces[i][1] - 1.0f);
		}
	}
	return height;
}

void GyromiteInitMoveUp()
{
	if (rob.state.handsY == 0.0f) {
		rob.executingCommand = ROB_CMD_NONE;
	} else {
		rob.counter = 12;
	}
}

void GyromiteInitMoveDown()
{
	if (rob.state.handsY == 4.0f
		|| (rob.state.handsX == 2.0f && rob.state.handsY == 2.0f)) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	if (rob.state.handsOpen == 1.0f) {
		rob.counter = 12;
		return;
	}
	if (!GyromiteContainsUnheldGyro(rob.state.handsX)) {
		rob.counter = 12;
		return;
	}
	if (rob.heldGyro >= 0 || rob.state.handsY == 2.0f
		|| (rob.state.handsX == 2.0f && rob.state.handsY == 0.0f)) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	rob.counter = 12;
}

void GyromiteInitMoveLeft()
{
	if (rob.state.handsX == -2.0f
		|| (GyromiteContainsUnheldGyro(rob.state.handsX - 1.0f)
			&& ((rob.heldGyro >= 0 && rob.state.handsY >= 2.0f)
				|| (rob.state.handsY == 4.0f && rob.state.handsX < 2.0f)))
		|| ((rob.state.handsY == 4.0f
			|| (rob.state.handsX == 2.0f && rob.state.handsY == 2.0f))
			&& (rob.heldGyro >= 0
				|| (rob.state.handsOpen == 1.0f
					&& GyromiteContainsUnheldGyro(rob.state.handsX))))) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	rob.counter = 16;
}

void GyromiteInitMoveRight()
{
	if (rob.state.handsX == 2.0f) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	const int sourceGyro = GyromiteGetUnheldGyro(rob.state.handsX);
	if (sourceGyro >= 0 && rob.state.handsOpen == 1.0f
		&& rob.state.handsY == rob.state.pieces[sourceGyro][1]) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	const int targetGyro = GyromiteGetUnheldGyro(rob.state.handsX + 1.0f);
	if (targetGyro >= 0) {
		if (rob.heldGyro >= 0) {
			if (rob.state.pieces[rob.heldGyro][1]
				>= rob.state.pieces[targetGyro][1] - 2.0f) {
				rob.executingCommand = ROB_CMD_NONE;
				return;
			}
		} else if (rob.state.handsY >= rob.state.pieces[targetGyro][1]) {
			rob.executingCommand = ROB_CMD_NONE;
			return;
		}
	} else if (rob.heldGyro >= 0) {
		if (rob.state.handsY == 4.0f
			|| (rob.state.handsX == 1.0f && rob.state.handsY != 0.0f)) {
			rob.executingCommand = ROB_CMD_NONE;
			return;
		}
	} else if (rob.state.handsX == 1.0f && rob.state.handsY == 4.0f) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	rob.counter = 16;
}

void GyromiteInitOpenHands()
{
	if (rob.heldGyro >= 0
		&& (rob.state.handsX == 0.0f || rob.state.handsX == 1.0f)
		&& !GyromiteGyroTimerStarted(rob.heldGyro)) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	if (rob.state.handsOpen == 1.0f
		|| (rob.heldGyro >= 0 && GyromiteContainsUnheldGyro(rob.state.handsX))) {
		rob.executingCommand = ROB_CMD_NONE;
	} else {
		rob.counter = 8;
	}
}

void GyromiteInitCloseHands()
{
	if (rob.state.handsOpen == 0.0f) {
		rob.executingCommand = ROB_CMD_NONE;
	} else {
		rob.counter = 8;
	}
}

void GyromiteExecuteMoveUp()
{
	rob.state.handsY -= 2.0f / 12.0f;
	if (--rob.counter == 0) {
		rob.state.handsY = rob_round(rob.state.handsY);
		rob.executingCommand = ROB_CMD_NONE;
	}
	if (rob.heldGyro >= 0) {
		rob.state.pieces[rob.heldGyro][1] = rob.state.handsY;
		if (rob.state.handsX == 0.0f) {
			rob.state.aButtonY = rob_max(3.0f,
				rob_min(rob.state.aButtonY, rob.state.handsY));
		} else if (rob.state.handsX == 1.0f) {
			rob.state.bButtonY = rob_max(3.0f,
				rob_min(rob.state.bButtonY, rob.state.handsY));
		}
	}
}

void GyromiteExecuteMoveDown()
{
	rob.state.handsY += 2.0f / 12.0f;
	if (--rob.counter == 0) {
		rob.state.handsY = rob_round(rob.state.handsY);
		rob.executingCommand = ROB_CMD_NONE;
	}
	if (rob.heldGyro >= 0) {
		rob.state.pieces[rob.heldGyro][1] = rob.state.handsY;
		if (rob.state.handsX == 0.0f) {
			rob.state.aButtonY = rob_min(4.0f,
				rob_max(rob.state.aButtonY, rob.state.handsY));
		} else if (rob.state.handsX == 1.0f) {
			rob.state.bButtonY = rob_min(4.0f,
				rob_max(rob.state.bButtonY, rob.state.handsY));
		}
	}
}

void GyromiteExecuteMoveLeft()
{
	rob.state.handsX -= 1.0f / 16.0f;
	if (--rob.counter == 0) {
		rob.state.handsX = rob_round(rob.state.handsX);
		rob.executingCommand = ROB_CMD_NONE;
	}
	if (rob.heldGyro >= 0) {
		rob.state.pieces[rob.heldGyro][0] = rob.state.handsX;
	}
}

void GyromiteExecuteMoveRight()
{
	rob.state.handsX += 1.0f / 16.0f;
	if (--rob.counter == 0) {
		rob.state.handsX = rob_round(rob.state.handsX);
		rob.executingCommand = ROB_CMD_NONE;
	}
	if (rob.heldGyro >= 0) {
		rob.state.pieces[rob.heldGyro][0] = rob.state.handsX;
	}
}

void GyromiteExecuteOpenHands()
{
	if (rob.heldGyro >= 0) {
		rob.fallingVelocity += 0.1f;
		rob.state.pieces[rob.heldGyro][1] += rob.fallingVelocity;
		const float ground = rob.state.handsX < 2.0f ? 4.0f : 2.0f;
		if (rob.state.pieces[rob.heldGyro][1] >= ground) {
			rob.state.pieces[rob.heldGyro][1] = ground;
		}
		if (rob.state.handsX == 0.0f
			&& rob.state.pieces[rob.heldGyro][1] > rob.state.aButtonY) {
			rob.state.aButtonY = rob.state.pieces[rob.heldGyro][1];
		}
		if (rob.state.handsX == 1.0f
			&& rob.state.pieces[rob.heldGyro][1] > rob.state.bButtonY) {
			rob.state.bButtonY = rob.state.pieces[rob.heldGyro][1];
		}
		if (rob.state.pieces[rob.heldGyro][1] >= ground) {
			rob.heldGyro = -1;
		}
	}
	if (rob.counter > 0) {
		rob.state.handsOpen += 1.0f / 8.0f;
		--rob.counter;
	} else if (rob.heldGyro < 0) {
		rob.fallingVelocity = 0.0f;
		rob.state.handsOpen = 1.0f;
		rob.executingCommand = ROB_CMD_NONE;
	}
}

void GyromiteExecuteCloseHands()
{
	rob.state.handsOpen -= 1.0f / 8.0f;
	if (--rob.counter == 0) {
		rob.state.handsOpen = 0.0f;
		const int index = GyromiteGetUnheldGyro(rob.state.handsX);
		if (index >= 0 && rob.state.pieces[index][1] == rob.state.handsY) {
			rob.heldGyro = index;
		}
		rob.executingCommand = ROB_CMD_NONE;
	}
}

bool StackUpIsHoldingBlock()
{
	for (int i = 4; i >= 0; --i) {
		if (rob.heldBlocks[i]) {
			return true;
		}
	}
	return false;
}

void StackUpScanMemory()
{
	const int mode = read_ram(0x0038);
	if (mode == 0) {
		return;
	}

	bool matches = true;
	for (int i = 5; i >= 0; --i) {
		for (int j = 4; j >= 0; --j) {
			const int offset = 5 * i + j;
			const int startValue = read_ram(0x0520 | offset);
			if (startValue > 5) {
				return;
			}
			const int goalValue = read_ram(0x0500 | offset);
			if (goalValue > 5) {
				return;
			}
			if (rob.puzzles[0][i][j] != startValue || rob.puzzles[1][i][j] != goalValue) {
				matches = false;
			}
			rob.puzzles[2][i][j] = startValue;
			rob.puzzles[3][i][j] = goalValue;
		}
	}

	if (matches) {
		if (rob.requestedCommand == ROB_CMD_NONE && rob.executingCommand == ROB_CMD_NONE) {
			if (mode == 2 && read_ram(0x0065) != 0x01) {
				return;
			}
			bool ready = true;
			switch (mode) {
			case 1:
			case 2:
				for (int i = 4; i >= 0; --i) {
					const int y = static_cast<int>(rob.state.pieces[i][1]);
					if (y == 0 || rob.puzzles[1][y][2 + static_cast<int>(rob.state.pieces[i][0])] != i + 1) {
						ready = false;
						break;
					}
				}
				if (ready) {
					RobRequest(ROB_CMD_ADVANCE_PHASE);
				}
				break;
			case 3:
				for (int i = 4; i >= 0; --i) {
					const int y = static_cast<int>(rob.state.pieces[i][1]);
					if (y == 0 || rob.puzzles[1][y][2 + static_cast<int>(rob.state.pieces[i][0])] == 0) {
						ready = false;
						break;
					}
				}
				if (ready) {
					RobRequest(ROB_CMD_ADVANCE_PHASE);
				}
				break;
			}
		}
	} else {
		for (int i = 0; i < 6; ++i) {
			for (int j = 0; j < 5; ++j) {
				std::swap(rob.puzzles[0][i][j], rob.puzzles[2][i][j]);
				std::swap(rob.puzzles[1][i][j], rob.puzzles[3][i][j]);
			}
		}

		for (int i = 5; i >= 0; --i) {
			for (int j = 4; j >= 0; --j) {
				int index = rob.puzzles[0][i][j];
				if (index > 0) {
					--index;
					rob.state.pieces[index][0] = static_cast<float>(j - 2);
					rob.state.pieces[index][1] = static_cast<float>(i);
				}
			}
		}

		rob.executingCommand = ROB_CMD_NONE;
		rob.requestedCommand = ROB_CMD_NONE;
		rob.counter = 0;
		if (rob.state.handsOpen == 0.0f) {
			RobRequest(ROB_CMD_OPEN_HANDS);
		} else {
			RobRequest(ROB_CMD_REPAINT);
		}
	}
}

void StackUpInitMoveUp()
{
	if (rob.state.handsY == 0.0f) {
		rob.executingCommand = ROB_CMD_NONE;
	} else {
		rob.counter = 12;
	}
}

void StackUpInitMoveDown()
{
	if (rob.state.handsY == 5.0f) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	const float height = StackUpGetStackHeight(rob.state.handsX);
	if (rob.state.handsOpen == 0.0f && rob.state.handsY == height) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	rob.counter = 12;
}

void StackUpInitMoveHorizontal(const float direction)
{
	if ((direction < 0.0f && rob.state.handsX == -2.0f)
		|| (direction > 0.0f && rob.state.handsX == 2.0f)) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	const bool holding = StackUpIsHoldingBlock();
	const float height = StackUpGetStackHeight(rob.state.handsX);
	if ((rob.state.handsOpen == 1.0f && rob.state.handsY > height)
		|| (holding && rob.state.handsY >= height)) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	const float targetHeight = StackUpGetStackHeight(rob.state.handsX + direction);
	if (rob.state.handsY > targetHeight
		|| (holding && rob.state.handsY >= targetHeight)) {
		rob.executingCommand = ROB_CMD_NONE;
		return;
	}
	rob.counter = 16;
}

void StackUpInitOpenHands()
{
	if (rob.state.handsOpen == 1.0f) {
		rob.executingCommand = ROB_CMD_NONE;
	} else {
		rob.fallingVelocity = 0.0f;
		rob.stackHeight = StackUpGetStackHeight(rob.state.handsX);
		rob.counter = 8;
	}
}

void StackUpInitCloseHands()
{
	if (rob.state.handsOpen == 0.0f) {
		rob.executingCommand = ROB_CMD_NONE;
	} else {
		rob.counter = 8;
	}
}

void StackUpInitAdvancePhase()
{
	rob.counter = 16;
}

void StackUpExecuteMoveUp()
{
	rob.state.handsY -= 1.0f / 12.0f;
	for (int i = 4; i >= 0; --i) {
		if (rob.heldBlocks[i]) {
			rob.state.pieces[i][1] -= 1.0f / 12.0f;
		}
	}
	if (--rob.counter == 0) {
		rob.state.handsY = rob_round(rob.state.handsY);
		for (int i = 4; i >= 0; --i) {
			if (rob.heldBlocks[i]) {
				rob.state.pieces[i][1] = rob_round(rob.state.pieces[i][1]);
			}
		}
		rob.executingCommand = ROB_CMD_NONE;
	}
}

void StackUpExecuteMoveDown()
{
	rob.state.handsY += 1.0f / 12.0f;
	for (int i = 4; i >= 0; --i) {
		if (rob.heldBlocks[i]) {
			rob.state.pieces[i][1] += 1.0f / 12.0f;
		}
	}
	if (--rob.counter == 0) {
		rob.state.handsY = rob_round(rob.state.handsY);
		for (int i = 4; i >= 0; --i) {
			if (rob.heldBlocks[i]) {
				rob.state.pieces[i][1] = rob_round(rob.state.pieces[i][1]);
			}
		}
		rob.executingCommand = ROB_CMD_NONE;
	}
}

void StackUpExecuteMoveHorizontal(const float direction)
{
	rob.state.handsX += direction * (1.0f / 16.0f);
	for (int i = 4; i >= 0; --i) {
		if (rob.heldBlocks[i]) {
			rob.state.pieces[i][0] += direction * (1.0f / 16.0f);
		}
	}
	if (--rob.counter == 0) {
		rob.state.handsX = rob_round(rob.state.handsX);
		for (int i = 4; i >= 0; --i) {
			if (rob.heldBlocks[i]) {
				rob.state.pieces[i][0] = rob_round(rob.state.pieces[i][0]);
			}
		}
		rob.executingCommand = ROB_CMD_NONE;
	}
}

void StackUpExecuteOpenHands()
{
	if (rob.counter > 0) {
		rob.state.handsOpen += 1.0f / 8.0f;
		--rob.counter;
	}
	rob.fallingVelocity += 0.1f;
	bool fallCompleted = false;
	bool fallingBlocks = false;
	for (int i = 4; i >= 0; --i) {
		if (rob.heldBlocks[i]) {
			fallingBlocks = true;
			rob.state.pieces[i][1] += rob.fallingVelocity;
			if (rob.state.pieces[i][1] >= rob.stackHeight) {
				fallCompleted = true;
			}
		}
	}
	if (fallCompleted) {
		fallingBlocks = false;
		double delta = 0.0;
		for (int i = 4; i >= 0; --i) {
			if (rob.heldBlocks[i] && rob.state.pieces[i][1] >= rob.stackHeight) {
				delta = std::max(delta,
					static_cast<double>(rob.state.pieces[i][1] - rob.stackHeight));
			}
		}
		for (int i = 4; i >= 0; --i) {
			if (rob.heldBlocks[i]) {
				rob.heldBlocks[i] = 0;
				rob.state.pieces[i][1] = rob_round(
					rob.state.pieces[i][1] - static_cast<float>(delta));
			}
		}
	}
	if (rob.counter == 0 && !fallingBlocks) {
		rob.state.handsOpen = 1.0f;
		rob.executingCommand = ROB_CMD_NONE;
	}
}

void StackUpExecuteCloseHands()
{
	rob.state.handsOpen -= 1.0f / 8.0f;
	if (--rob.counter == 0) {
		rob.state.handsOpen = 0.0f;
		for (int i = 4; i >= 0; --i) {
			if (rob.state.pieces[i][0] == rob.state.handsX
				&& rob.state.pieces[i][1] <= rob.state.handsY) {
				rob.heldBlocks[i] = 1;
			}
		}
		rob.executingCommand = ROB_CMD_NONE;
	}
}

void StackUpExecuteAdvancePhase()
{
	if (--rob.counter == 0) {
		rob.executingCommand = ROB_CMD_NONE;
		RobRequest(ROB_CMD_OPEN_HANDS);
	}
}

void RobInitCommand()
{
	switch (rob.executingCommand) {
	case ROB_CMD_MOVE_UP:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteInitMoveUp();
		} else {
			StackUpInitMoveUp();
		}
		break;
	case ROB_CMD_MOVE_DOWN:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteInitMoveDown();
		} else {
			StackUpInitMoveDown();
		}
		break;
	case ROB_CMD_MOVE_LEFT:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteInitMoveLeft();
		} else {
			StackUpInitMoveHorizontal(-1.0f);
		}
		break;
	case ROB_CMD_MOVE_RIGHT:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteInitMoveRight();
		} else {
			StackUpInitMoveHorizontal(1.0f);
		}
		break;
	case ROB_CMD_OPEN_HANDS:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteInitOpenHands();
		} else {
			StackUpInitOpenHands();
		}
		break;
	case ROB_CMD_CLOSE_HANDS:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteInitCloseHands();
		} else {
			StackUpInitCloseHands();
		}
		break;
	case ROB_CMD_TEST:
		RobInitTest();
		break;
	case ROB_CMD_REPAINT:
		RobInitRepaint();
		break;
	case ROB_CMD_ADVANCE_PHASE:
		if (rob.state.game == ROB_STACK_UP) {
			StackUpInitAdvancePhase();
		} else {
			RobInitAdvancePhase();
		}
		break;
	}
}

void RobExecuteCommand()
{
	switch (rob.executingCommand) {
	case ROB_CMD_MOVE_UP:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteExecuteMoveUp();
		} else {
			StackUpExecuteMoveUp();
		}
		break;
	case ROB_CMD_MOVE_DOWN:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteExecuteMoveDown();
		} else {
			StackUpExecuteMoveDown();
		}
		break;
	case ROB_CMD_MOVE_LEFT:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteExecuteMoveLeft();
		} else {
			StackUpExecuteMoveHorizontal(-1.0f);
		}
		break;
	case ROB_CMD_MOVE_RIGHT:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteExecuteMoveRight();
		} else {
			StackUpExecuteMoveHorizontal(1.0f);
		}
		break;
	case ROB_CMD_OPEN_HANDS:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteExecuteOpenHands();
		} else {
			StackUpExecuteOpenHands();
		}
		break;
	case ROB_CMD_CLOSE_HANDS:
		if (rob.state.game == ROB_GYROMITE) {
			GyromiteExecuteCloseHands();
		} else {
			StackUpExecuteCloseHands();
		}
		break;
	case ROB_CMD_TEST:
		RobExecuteTest();
		break;
	case ROB_CMD_REPAINT:
		RobExecuteRepaint();
		break;
	case ROB_CMD_ADVANCE_PHASE:
		if (rob.state.game == ROB_STACK_UP) {
			StackUpExecuteAdvancePhase();
		} else {
			rob.executingCommand = ROB_CMD_NONE;
		}
		break;
	}
}

bool RobUpdate()
{
	const int request = rob.requestedCommand;
	rob.requestedCommand = ROB_CMD_NONE;
	if (rob.executingCommand == ROB_CMD_NONE && request != ROB_CMD_NONE) {
		rob.executingCommand = request;
		RobInitCommand();
	}
	if (rob.executingCommand != ROB_CMD_NONE) {
		RobExecuteCommand();
		++rob.state.modifications;
		return true;
	}
	return false;
}

uint8 RobGetGyromiteButtonMask()
{
	uint8 buttons = 0;

	if (rob.state.aButtonY >= 3.99f) {
		buttons |= 0x01;
	}
	if (rob.state.bButtonY >= 3.99f) {
		buttons |= 0x02;
	}
	return buttons;
}

void RobApplyGyromiteButtons()
{
	joy[1] |= RobGetGyromiteButtonMask();
}

void RobApplyStackUpButtons()
{
	if ((joy[0] & 0x08) || (joy[1] & 0x08) || (joy[2] & 0x08) || (joy[3] & 0x08)) {
		std::memset(rob.heldBlocks, 0, sizeof(rob.heldBlocks));
		rob.fallingVelocity = 0.0f;
		rob.stackHeight = 0.0f;
		RobResetState();
		StackUpScanMemory();
		return;
	}

	if (rob.executingCommand != ROB_CMD_ADVANCE_PHASE) {
		return;
	}

	const int mode = read_ram(0x0038);
	switch (mode) {
	case 1:
	case 2:
		joy[0] |= 0x08;
		break;
	case 3:
		if (rob.counter > 8) {
			joy[0] |= 0x01;
		} else {
			const int scroll = read_ram(0x000B);
			if (scroll != 0x80) {
				++rob.counter;
			} else {
				joy[0] |= 0x08;
			}
		}
		break;
	}
}

} // namespace

SFORMAT FCEUROB_STATEINFO[] = {
	{ &rob.state.game, sizeof(rob.state.game) | FCEUSTATE_RLSB, "RBGM" },
	{ rob.state.pieces, sizeof(rob.state.pieces), "RBPC" },
	{ &rob.state.modifications, sizeof(rob.state.modifications) | FCEUSTATE_RLSB, "RBMD" },
	{ &rob.state.handsX, sizeof(rob.state.handsX), "RBHX" },
	{ &rob.state.handsY, sizeof(rob.state.handsY), "RBHY" },
	{ &rob.state.handsOpen, sizeof(rob.state.handsOpen), "RBHO" },
	{ &rob.state.aButtonY, sizeof(rob.state.aButtonY), "RBAY" },
	{ &rob.state.bButtonY, sizeof(rob.state.bButtonY), "RBBY" },
	{ &rob.state.testing, sizeof(rob.state.testing), "RBTG" },
	{ &rob.executingCommand, sizeof(rob.executingCommand) | FCEUSTATE_RLSB, "RBEC" },
	{ &rob.counter, sizeof(rob.counter) | FCEUSTATE_RLSB, "RBCT" },
	{ &rob.shiftRegister, sizeof(rob.shiftRegister) | FCEUSTATE_RLSB, "RBSR" },
	{ &rob.requestedCommand, sizeof(rob.requestedCommand) | FCEUSTATE_RLSB, "RBRC" },
	{ &rob.heldGyro, sizeof(rob.heldGyro) | FCEUSTATE_RLSB, "RBHG" },
	{ &rob.fallingVelocity, sizeof(rob.fallingVelocity), "RBFV" },
	{ rob.heldBlocks, sizeof(rob.heldBlocks), "RBHB" },
	{ rob.puzzles, sizeof(rob.puzzles), "RBPZ" },
	{ &rob.stackHeight, sizeof(rob.stackHeight), "RBSH" },
	{ rob.gyroTimers, sizeof(rob.gyroTimers), "RBGT" },
	{ rob.gyroSpinnerState, sizeof(rob.gyroSpinnerState), "RBGS" },
	{ 0 }
};

void FCEU_RobInit()
{
	RobInitForGame();
}

void FCEU_RobFrameRendered()
{
	if (rob.state.game == ROB_NONE || XBuf == nullptr) {
		return;
	}

	const uint8 color = XBuf[0] & 0x3F;
	RobSignal(palo[color].g > 0x7F);
}

void FCEU_RobUpdateInput()
{
	if (rob.state.game == ROB_NONE || FCEUMOV_Mode(MOVIEMODE_PLAY)) {
		return;
	}

	if (rob.state.game == ROB_STACK_UP) {
		StackUpScanMemory();
	}
	RobUpdate();
	if (rob.state.game == ROB_GYROMITE) {
		GyromiteUpdateGyroTimers();
	}

	switch (rob.state.game) {
	case ROB_GYROMITE:
		RobApplyGyromiteButtons();
		break;
	case ROB_STACK_UP:
		RobApplyStackUpButtons();
		break;
	}
}

bool FCEU_RobIsActive()
{
	return rob.state.game != ROB_NONE;
}

void FCEU_RobGetSnapshot(FCEURobSnapshot* snapshot)
{
	if (snapshot == nullptr) {
		return;
	}

	std::memset(snapshot, 0, sizeof(*snapshot));
	std::memcpy(snapshot->pieces, rob.state.pieces, sizeof(snapshot->pieces));
	std::memcpy(snapshot->heldBlocks, rob.heldBlocks, sizeof(snapshot->heldBlocks));
	snapshot->modifications = rob.state.modifications;
	snapshot->game = rob.state.game;
	snapshot->executingCommand = rob.executingCommand;
	snapshot->heldGyro = rob.heldGyro;
	snapshot->handsX = rob.state.handsX;
	snapshot->handsY = rob.state.handsY;
	snapshot->handsOpen = rob.state.handsOpen;
	snapshot->aButtonY = rob.state.aButtonY;
	snapshot->bButtonY = rob.state.bButtonY;
	snapshot->stackHeight = rob.stackHeight;
	std::memcpy(snapshot->gyroTimers, rob.gyroTimers, sizeof(snapshot->gyroTimers));
	snapshot->testing = rob.state.testing;
}

uint8 FCEU_RobGetVirtualPortButtons(const int port)
{
	if (rob.state.game == ROB_GYROMITE && port == 1) {
		return RobGetGyromiteButtonMask();
	}
	return 0;
}
