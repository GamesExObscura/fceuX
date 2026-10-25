/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2026 OpenAI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
// RobWindow.cpp
//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <QCloseEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QScreen>
#include <QShortcut>
#include <QSettings>
#include <QWindow>

#include "Qt/RobWindow.h"

static RobWindowDialog_t* robWindow = nullptr;

namespace {

constexpr qreal IDEAL_WIDTH = 1260.0;
constexpr qreal IDEAL_HEIGHT = 554.0;
constexpr int GYRO_WARNING_FRAMES = 15 * 60;
constexpr int FRAME_MS = 33;

const QColor ROB_OUTER_BLACK(0x14, 0x14, 0x14);
const QColor ROB_FACE_PLATE(0xD7, 0xC8, 0xBE);
const QColor ROB_FACE_SHADOW(0xA9, 0x99, 0x90);
const QColor ROB_FACE_HIGHLIGHT(0xF0, 0xE6, 0xDE);
const QColor ROB_BODY_PANEL(0xC7, 0xB9, 0xAE);
const QColor ROB_BODY_DARK(0x7E, 0x74, 0x6F);
const QColor ROB_BODY_LIGHT(0xE7, 0xDD, 0xD5);
const QColor ROB_EYE_RING(0x1A, 0x1A, 0x1A);
const QColor ROB_EYE_RED(0x9E, 0x1F, 0x34);
const QColor ROB_EYE_GLOW(0xF4, 0x93, 0x9B);
const QColor ROB_NECK(0xA9, 0x9E, 0x97);
const QColor ROB_PEDESTAL(0x8D, 0x85, 0x7F);
const QColor ROB_SHELL_DARK(0x82, 0x81, 0x7D);
const QColor ROB_SHELL_MID(0xB8, 0xB8, 0xB4);
const QColor ROB_SHELL_LIGHT(0xE3, 0xE3, 0xDF);
const QColor ROB_SHADOW_LINE(0x67, 0x67, 0x65);
const QColor ROB_LED_RED(0xB6, 0x1C, 0x2F);
const QColor ROB_LED_GLOW(0xFF, 0x7B, 0x7D);

const QColor GYRO_SHAFT(0xB4, 0xB6, 0xA9);
const QColor GYRO_BASE(0xD7, 0xD8, 0xCD);
const QColor GYRO_HOLDER(0x00, 0x00, 0x00);
const QColor GYRO_A_BUTTON(0x1E, 0x9A, 0xFF);
const QColor GYRO_B_BUTTON(0xFF, 0x2A, 0x16);
const QColor GYRO_HANDS(0x7A, 0x70, 0x7E);
const QColor GYRO_TOPPER_COLORS[2] = {
	QColor(0x29, 0xA7, 0x48),
	QColor(0xF2, 0x89, 0x22),
};
const QColor GYRO_TIMER_BG(0xF2, 0xF2, 0xF2, 0xE8);
const QColor GYRO_TIMER_TEXT(0x16, 0x16, 0x16);

const QColor STACK_PLATFORM(0xB2, 0xA8, 0xA6);
const QColor STACK_HANDS(0xA3, 0x9D, 0xB5);
const QColor STACK_BLOCK_COLORS[5] = {
	QColor(0xFF, 0x1F, 0x57),
	QColor(0xCC, 0xC5, 0xCA),
	QColor(0x1D, 0x66, 0xAD),
	QColor(0xFB, 0xB8, 0x46),
	QColor(0x4E, 0xAA, 0x6D),
};

const int GYRO_POLYGON[2][11] = {
	{ 0, 11, 16, 16, 18, 18, -18, -18, -16, -16, -11 },
	{ 0,  5, 44, 149, 149, 152, 152, 149, 149, 44,   5 },
};

const int GYRO_HOLDER_POLYGON[2][9] = {
	{ 0, 10, 18, 66, 66, -66, -66, -18, -10 },
	{ 0,  0, 76, 76, 83,  83,  76,  76,   0 },
};

const int LEFT_HAND_POLYGON[2][8] = {
	{ -76, -16, -16, 16, 16, -16, -16, -76 },
	{ -149, -149, -136, -132, -126, -122, -109, -109 },
};

const int RIGHT_HAND_POLYGON[2][10] = {
	{ 16, -16, -16, 16, 16, -16, -16, 16, 76, 76 },
	{ -109, -115, -122, -126, -132, -136, -142, -149, -149, -109 },
};

const int STACK_BLOCK_POLYGON[2][8] = {
	{ 50, 50, -50, -50, -25, -12, 12, 25 },
	{  0, -40, -40,   0,   0,  20, 20,  0 },
};

QPolygonF makePolygon(const int* xs, const int* ys, const int count)
{
	QPolygonF polygon;
	polygon.reserve(count);
	for (int i = 0; i < count; ++i) {
		polygon << QPointF(xs[i], ys[i]);
	}
	return polygon;
}

qreal fitScale(const QSize& size)
{
	if (size.width() <= 0 || size.height() <= 0) {
		return 1.0;
	}
	return std::min(size.width() / IDEAL_WIDTH, size.height() / IDEAL_HEIGHT);
}

void beginScene(QPainter& painter, const QSize& size)
{
	const qreal scale = fitScale(size);
	const qreal width = IDEAL_WIDTH * scale;
	const qreal height = IDEAL_HEIGHT * scale;
	const qreal tx = (size.width() - width) * 0.5;
	const qreal ty = (size.height() - height) * 0.5;
	painter.translate(tx, ty);
	painter.scale(scale, scale);
}

qreal convertX(const qreal x)
{
	return 630.0 + 230.0 * x;
}

qreal convertStackY(const qreal y)
{
	return 310.0 + 40.0 * y;
}

qreal convertGyromiteY(const qreal y)
{
	return 343.0 + 40.0 * y;
}

void drawROB(QPainter& painter, const qreal x, const qreal y, const bool lit)
{
	painter.save();
	painter.translate(x, y);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(ROB_SHADOW_LINE, 1.6));

	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(0x00, 0x00, 0x00, 30));
	painter.drawEllipse(QRectF(-144.0, 238.0, 288.0, 32.0));
	painter.setPen(QPen(ROB_SHADOW_LINE, 1.6));

	QLinearGradient baseGradient(0.0, 150.0, 0.0, 260.0);
	baseGradient.setColorAt(0.0, ROB_SHELL_LIGHT);
	baseGradient.setColorAt(0.45, ROB_SHELL_MID);
	baseGradient.setColorAt(1.0, ROB_SHELL_DARK);
	painter.setBrush(baseGradient);
	painter.drawRoundedRect(QRectF(-122.0, 176.0, 244.0, 78.0), 22.0, 22.0);

	QLinearGradient wingGradient(0.0, 168.0, 0.0, 236.0);
	wingGradient.setColorAt(0.0, QColor(0xD8, 0xD7, 0xD2));
	wingGradient.setColorAt(1.0, QColor(0x8E, 0x8C, 0x88));
	painter.setBrush(wingGradient);
	QPainterPath leftWing;
	leftWing.moveTo(-154.0, 186.0);
	leftWing.lineTo(-122.0, 176.0);
	leftWing.lineTo(-92.0, 188.0);
	leftWing.lineTo(-102.0, 232.0);
	leftWing.lineTo(-140.0, 244.0);
	leftWing.lineTo(-164.0, 220.0);
	leftWing.closeSubpath();
	painter.drawPath(leftWing);
	painter.save();
	painter.scale(-1.0, 1.0);
	painter.drawPath(leftWing);
	painter.restore();

	QLinearGradient pedestalGradient(0.0, 118.0, 0.0, 232.0);
	pedestalGradient.setColorAt(0.0, QColor(0xF0, 0xEF, 0xEB));
	pedestalGradient.setColorAt(0.55, QColor(0xC6, 0xC5, 0xC1));
	pedestalGradient.setColorAt(1.0, QColor(0x8C, 0x8C, 0x89));
	painter.setBrush(pedestalGradient);
	painter.drawRoundedRect(QRectF(-62.0, 122.0, 124.0, 96.0), 30.0, 30.0);
	painter.drawRoundedRect(QRectF(-30.0, 96.0, 60.0, 38.0), 15.0, 15.0);

	painter.setBrush(QColor(0xE6, 0xE5, 0xE1));
	painter.drawRoundedRect(QRectF(-106.0, 90.0, 212.0, 66.0), 18.0, 18.0);
	painter.setBrush(QColor(0x9B, 0x99, 0x95));
	painter.drawRoundedRect(QRectF(-94.0, 132.0, 188.0, 16.0), 7.0, 7.0);
	painter.setBrush(QColor(0xD7, 0xD5, 0xD1));
	painter.drawRoundedRect(QRectF(-74.0, 106.0, 148.0, 20.0), 8.0, 8.0);

	QLinearGradient neckGradient(0.0, 54.0, 0.0, 132.0);
	neckGradient.setColorAt(0.0, QColor(0xEB, 0xEB, 0xE7));
	neckGradient.setColorAt(1.0, QColor(0x8E, 0x8D, 0x89));
	painter.setBrush(neckGradient);
	painter.drawRoundedRect(QRectF(-26.0, 56.0, 52.0, 62.0), 8.0, 8.0);
	for (int i = 0; i < 7; ++i) {
		const qreal lineY = 62.0 + i * 8.0;
		painter.setPen(QPen(QColor(0x73, 0x72, 0x6F), 1.4));
		painter.drawLine(QPointF(-22.0, lineY), QPointF(22.0, lineY));
	}
	painter.setPen(QPen(ROB_SHADOW_LINE, 1.6));

	QLinearGradient headGradient(0.0, -4.0, 0.0, 88.0);
	headGradient.setColorAt(0.0, QColor(0xF4, 0xF4, 0xF1));
	headGradient.setColorAt(0.38, QColor(0xD8, 0xD7, 0xD2));
	headGradient.setColorAt(1.0, QColor(0x91, 0x90, 0x8C));
	painter.setBrush(headGradient);
	painter.drawRoundedRect(QRectF(-116.0, -2.0, 232.0, 88.0), 24.0, 24.0);
	painter.setBrush(QColor(0xCC, 0xCB, 0xC7));
	painter.drawRoundedRect(QRectF(-96.0, 10.0, 192.0, 20.0), 10.0, 10.0);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(0xE6, 0xE5, 0xE1, 150));
	painter.drawRoundedRect(QRectF(-78.0, 6.0, 124.0, 8.0), 4.0, 4.0);

	QLinearGradient visorGradient(-90.0, 26.0, 90.0, 64.0);
	visorGradient.setColorAt(0.0, QColor(0x05, 0x05, 0x06));
	visorGradient.setColorAt(0.5, QColor(0x22, 0x22, 0x24));
	visorGradient.setColorAt(1.0, QColor(0x04, 0x04, 0x05));
	painter.setBrush(visorGradient);
	painter.setPen(Qt::NoPen);
	painter.drawRoundedRect(QRectF(-88.0, 18.0, 176.0, 42.0), 16.0, 16.0);
	QLinearGradient visorGloss(-64.0, 18.0, 18.0, 50.0);
	visorGloss.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFF, 95));
	visorGloss.setColorAt(1.0, QColor(0xFF, 0xFF, 0xFF, 0));
	painter.setBrush(visorGloss);
	painter.drawRoundedRect(QRectF(-76.0, 22.0, 84.0, 12.0), 6.0, 6.0);

	const QColor eyeCore = lit ? ROB_EYE_GLOW : ROB_EYE_RED;
	painter.setPen(QPen(QColor(0x2C, 0x2C, 0x2F), 2.0));
	painter.setBrush(QColor(0x0E, 0x0E, 0x10));
	painter.drawEllipse(QRectF(-84.0, 12.0, 56.0, 56.0));
	painter.drawEllipse(QRectF(28.0, 12.0, 56.0, 56.0));
	painter.setBrush(QColor(0x31, 0x31, 0x34));
	painter.drawEllipse(QRectF(-76.0, 20.0, 40.0, 40.0));
	painter.drawEllipse(QRectF(36.0, 20.0, 40.0, 40.0));
	painter.setBrush(eyeCore);
	painter.setPen(Qt::NoPen);
	painter.drawEllipse(QRectF(-70.0, 26.0, 28.0, 28.0));
	painter.drawEllipse(QRectF(42.0, 26.0, 28.0, 28.0));
	painter.setBrush(lit ? QColor(0xFF, 0xF5, 0xF6) : QColor(0xF9, 0xC4, 0xC9));
	painter.drawEllipse(QRectF(-61.0, 32.0, 9.0, 9.0));
	painter.drawEllipse(QRectF(51.0, 32.0, 9.0, 9.0));
	painter.setBrush(QColor(0xC7, 0x1E, 0x2E));
	painter.drawRoundedRect(QRectF(-7.0, -8.0, 14.0, 6.0), 2.0, 2.0);

	QLinearGradient shoulderGradient(0.0, 94.0, 0.0, 154.0);
	shoulderGradient.setColorAt(0.0, QColor(0xDE, 0xDD, 0xD9));
	shoulderGradient.setColorAt(1.0, QColor(0x84, 0x83, 0x80));
	painter.setBrush(shoulderGradient);
	QPainterPath leftShoulder;
	leftShoulder.moveTo(-142.0, 100.0);
	leftShoulder.lineTo(-108.0, 90.0);
	leftShoulder.lineTo(-78.0, 100.0);
	leftShoulder.lineTo(-90.0, 132.0);
	leftShoulder.lineTo(-126.0, 140.0);
	leftShoulder.lineTo(-148.0, 126.0);
	leftShoulder.closeSubpath();
	painter.setPen(QPen(ROB_SHADOW_LINE, 1.4));
	painter.drawPath(leftShoulder);
	painter.save();
	painter.scale(-1.0, 1.0);
	painter.drawPath(leftShoulder);
	painter.restore();

	painter.setBrush(QColor(0x8A, 0x89, 0x85));
	painter.drawRoundedRect(QRectF(-142.0, 202.0, 54.0, 14.0), 4.0, 4.0);
	painter.drawRoundedRect(QRectF(88.0, 202.0, 54.0, 14.0), 4.0, 4.0);
	painter.setBrush(QColor(0xD9, 0xD8, 0xD3));
	painter.drawRoundedRect(QRectF(-136.0, 205.0, 42.0, 4.0), 2.0, 2.0);
	painter.drawRoundedRect(QRectF(94.0, 205.0, 42.0, 4.0), 2.0, 2.0);

	painter.setPen(Qt::NoPen);
	painter.setBrush(ROB_LED_RED);
	painter.drawRoundedRect(QRectF(-55.0, 215.0, 30.0, 12.0), 3.0, 3.0);
	painter.setBrush(ROB_LED_GLOW);
	for (int i = 0; i < 3; ++i) {
		painter.drawEllipse(QRectF(-52.0 + i * 9.0, 218.0, 6.0, 6.0));
	}
	painter.setBrush(QColor(0x9B, 0x99, 0x95));
	painter.drawRoundedRect(QRectF(-10.0, 212.0, 22.0, 26.0), 5.0, 5.0);
	painter.setBrush(QColor(0xD8, 0xD7, 0xD1, 180));
	painter.drawRoundedRect(QRectF(-94.0, 8.0, 132.0, 10.0), 5.0, 5.0);
	painter.setBrush(QColor(0x7B, 0x7A, 0x76));
	painter.drawRoundedRect(QRectF(-5.0, 218.0, 12.0, 16.0), 3.0, 3.0);
	painter.setBrush(QColor(0xB8, 0xB7, 0xB2));
	painter.drawRoundedRect(QRectF(-4.0, 219.0, 10.0, 5.0), 2.0, 2.0);
	painter.restore();
}

bool geometryVisibleOnAnyScreen(const QRect& geometry)
{
	if (!geometry.isValid()) {
		return false;
	}

	const QList<QScreen*> screens = QGuiApplication::screens();
	for (QScreen* screen : screens) {
		if (screen != nullptr && screen->availableGeometry().intersects(geometry)) {
			return true;
		}
	}
	return false;
}

QRect defaultRobWindowGeometry(QWidget* parent, const QSize& hint)
{
	QScreen* screen = nullptr;
	if (parent != nullptr) {
		screen = parent->screen();
		if (screen == nullptr && parent->windowHandle() != nullptr) {
			screen = parent->windowHandle()->screen();
		}
	}
	if (screen == nullptr) {
		screen = QGuiApplication::primaryScreen();
	}

	QRect available = screen != nullptr ? screen->availableGeometry() : QRect(80, 80, 1280, 720);
	const int width = std::max(1, std::min(hint.width(), available.width() - 24));
	const int height = std::max(1, std::min(hint.height(), available.height() - 24));
	QRect geometry(0, 0, width, height);

	if (parent != nullptr) {
		const QRect parentFrame = parent->frameGeometry();
		geometry.moveTopLeft(parentFrame.topRight() + QPoint(18, 32));
		if (!available.contains(geometry)) {
			geometry.moveLeft(std::max(available.left() + 12, parentFrame.left()));
			geometry.moveTop(std::max(available.top() + 24, parentFrame.top() + 24));
		}
	}

	if (!available.contains(geometry)) {
		geometry.moveCenter(available.center());
	}
	return geometry;
}

void drawGyroButton(QPainter& painter, const qreal x, const qreal y, const QColor& color)
{
	painter.save();
	painter.translate(convertX(x), convertGyromiteY(y));
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	painter.drawRect(QRectF(-42.0, -5.0, 84.0, 25.0));
	painter.restore();
}

void drawSpinner(QPainter& painter, const qreal x, const qreal y)
{
	painter.save();
	painter.translate(x, y + 34.0);
	painter.setPen(Qt::NoPen);
	painter.setBrush(GYRO_HOLDER);
	painter.drawRect(QRectF(-48.0, 0.0, 96.0, 12.0));
	painter.drawRect(QRectF(-40.0, 12.0, 80.0, 4.0));
	painter.setBrush(GYRO_SHAFT);
	painter.drawRect(QRectF(-48.0, 16.0, 96.0, 82.0));
	painter.drawRect(QRectF(-56.0, 82.0, 112.0, 81.0));
	painter.restore();
}

void drawGyroHolder(QPainter& painter, const qreal x, const qreal y)
{
	painter.save();
	painter.translate(x, y + 117.0);
	painter.scale(1.0, -1.0);
	painter.setPen(Qt::NoPen);
	painter.setBrush(GYRO_HOLDER);
	painter.drawPolygon(makePolygon(GYRO_HOLDER_POLYGON[0], GYRO_HOLDER_POLYGON[1], 9));
	painter.restore();
}

void drawGyro(QPainter& painter, const qreal x, const qreal y, const qreal rotationDeg,
	const qreal wobbleOffset, const QColor& topperColor)
{
	painter.save();
	painter.translate(convertX(x), convertGyromiteY(y) + wobbleOffset);
	painter.rotate(rotationDeg);

	painter.scale(1.0, -1.0);
	painter.setPen(Qt::NoPen);
	painter.setBrush(GYRO_SHAFT);
	painter.drawPolygon(makePolygon(GYRO_POLYGON[0], GYRO_POLYGON[1], 11));
	painter.setBrush(GYRO_BASE);
	painter.drawRect(QRectF(-111.0, 44.0, 222.0, 25.0));
	painter.setBrush(topperColor);
	painter.drawRoundedRect(QRectF(-18.0, 145.0, 36.0, 14.0), 5.0, 5.0);
	painter.setBrush(topperColor.darker(135));
	painter.drawRect(QRectF(-7.0, 136.0, 14.0, 9.0));
	painter.restore();
}

void drawGyromiteHands(QPainter& painter, const qreal x, const qreal y, const qreal open)
{
	painter.save();
	painter.setPen(Qt::NoPen);
	painter.setBrush(GYRO_HANDS);

	const qreal cx = convertX(x);
	const qreal cy = convertGyromiteY(y);
	const qreal openOffset = open * 40.0;

	painter.save();
	painter.translate(cx - openOffset, cy);
	painter.drawPolygon(makePolygon(LEFT_HAND_POLYGON[0], LEFT_HAND_POLYGON[1], 8));
	painter.restore();

	painter.save();
	painter.translate(cx + openOffset, cy);
	painter.drawPolygon(makePolygon(RIGHT_HAND_POLYGON[0], RIGHT_HAND_POLYGON[1], 10));
	painter.restore();
	painter.restore();
}

void drawStackPlatform(QPainter& painter, const qreal x)
{
	painter.save();
	painter.setPen(Qt::NoPen);
	painter.setBrush(STACK_PLATFORM);
	const qreal px = convertX(x);
	const qreal py = convertStackY(5.0);
	painter.drawRect(QRectF(px - 63.0, py, 126.0, 6.0));
	painter.drawRect(QRectF(px - 50.0, py, 100.0, 30.0));
	painter.restore();
}

void drawStackBlock(QPainter& painter, const qreal x, const qreal y, const QColor& color, const qreal angleDeg)
{
	painter.save();
	painter.translate(convertX(x), convertStackY(y));
	painter.rotate(angleDeg);
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	painter.drawPolygon(makePolygon(STACK_BLOCK_POLYGON[0], STACK_BLOCK_POLYGON[1], 8));
	painter.restore();
}

void drawStackHands(QPainter& painter, const FCEURobSnapshot& snapshot)
{
	painter.save();
	painter.translate(convertX(snapshot.handsX), convertStackY(snapshot.handsY));
	painter.setPen(Qt::NoPen);
	const int open = static_cast<int>(50.0 + snapshot.handsOpen * 30.0);

	painter.setBrush(ROB_OUTER_BLACK);
	painter.drawRect(QRectF(open, -33.0, 6.0, 26.0));
	painter.drawRect(QRectF(-open - 6.0, -33.0, 6.0, 26.0));

	painter.setBrush(STACK_HANDS);
	painter.drawRect(QRectF(open + 6.0, -35.0, 63.0, 30.0));
	painter.drawRect(QRectF(-open - 69.0, -35.0, 63.0, 30.0));
	painter.restore();
}

void drawGyroTimers(QPainter& painter, const FCEURobSnapshot& snapshot)
{
	int activeCount = 0;
	for (int i = 0; i < 2; ++i) {
		if (snapshot.gyroTimers[i] > 0) {
			++activeCount;
		}
	}
	if (activeCount == 0) {
		return;
	}

	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);

	const qreal cardWidth = 132.0;
	const qreal cardHeight = 38.0;
	const qreal gap = 10.0;
	qreal y = 26.0;

	QFont font = painter.font();
	font.setPointSize(13);
	font.setBold(true);
	painter.setFont(font);

	for (int i = 0; i < 2; ++i) {
		if (snapshot.gyroTimers[i] <= 0) {
			continue;
		}

		const QRectF card(IDEAL_WIDTH - cardWidth - 26.0, y, cardWidth, cardHeight);
		painter.setPen(Qt::NoPen);
		painter.setBrush(GYRO_TIMER_BG);
		painter.drawRoundedRect(card, 11.0, 11.0);
		painter.setBrush(GYRO_TOPPER_COLORS[i]);
		painter.drawRoundedRect(QRectF(card.x() + 8.0, card.y() + 8.0, 22.0, 22.0), 7.0, 7.0);

		const int seconds = std::max(1, (snapshot.gyroTimers[i] + 59) / 60);
		painter.setPen(GYRO_TIMER_TEXT);
		painter.drawText(QRectF(card.x() + 40.0, card.y(), card.width() - 48.0, card.height()),
			Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("%1 s").arg(seconds));
		y += cardHeight + gap;
	}

	painter.restore();
}

void drawGyromiteScene(QPainter& painter, const FCEURobSnapshot& snapshot,
	const RobDisplayWidget_t::GyroVisualState gyros[2])
{
	drawROB(painter, 630.0, 10.0, snapshot.testing != 0);
	drawGyroHolder(painter, 170.0, 425.0);
	drawGyroHolder(painter, 400.0, 425.0);
	drawSpinner(painter, 1090.0, 345.0);
	drawGyroButton(painter, 0.0, snapshot.aButtonY, GYRO_A_BUTTON);
	drawGyroButton(painter, 1.0, snapshot.bButtonY, GYRO_B_BUTTON);

	for (int i = 1; i >= 0; --i) {
		qreal rotation = 0.0;
		qreal wobbleOffset = 0.0;
		const bool warning = snapshot.gyroTimers[i] > 0
			&& snapshot.gyroTimers[i] <= GYRO_WARNING_FRAMES;
		if (warning) {
			const qreal urgency = 1.0
				- std::max(0, snapshot.gyroTimers[i]) / static_cast<qreal>(GYRO_WARNING_FRAMES);
			const qreal amplitude = 4.0 + urgency * 14.0;
			rotation = std::sin(gyros[i].wobbleTick * 0.35 + i) * amplitude;
			wobbleOffset = std::sin(gyros[i].wobbleTick * 0.6 + i * 1.7) * (1.5 + urgency * 6.0);
		}
		drawGyro(painter, snapshot.pieces[i][0], snapshot.pieces[i][1], rotation, wobbleOffset,
			GYRO_TOPPER_COLORS[i]);
	}

	drawGyromiteHands(painter, snapshot.handsX, snapshot.handsY, snapshot.handsOpen);
	drawGyroTimers(painter, snapshot);
}

void drawStackUpScene(QPainter& painter, const FCEURobSnapshot& snapshot)
{
	drawROB(painter, 630.0, 10.0, snapshot.testing != 0);

	std::array<int, 5> order = { 0, 1, 2, 3, 4 };
	std::sort(order.begin(), order.end(), [&](const int lhs, const int rhs) {
		return snapshot.pieces[lhs][1] < snapshot.pieces[rhs][1];
	});

	for (const int index : order) {
		drawStackBlock(painter, snapshot.pieces[index][0], snapshot.pieces[index][1],
			STACK_BLOCK_COLORS[index], 0.0);
	}

	for (int i = 4; i >= 0; --i) {
		drawStackPlatform(painter, i - 2.0);
	}

	drawStackHands(painter, snapshot);
}

} // namespace

RobDisplayWidget_t::RobDisplayWidget_t(QWidget* parent)
	: QWidget(parent)
{
	std::memset(&snapshot, 0, sizeof(snapshot));
	std::memset(gyros, 0, sizeof(gyros));
	setAutoFillBackground(false);
}

void RobDisplayWidget_t::setSnapshot(const FCEURobSnapshot& value)
{
	snapshot = value;
}

void RobDisplayWidget_t::tickVisuals(void)
{
	updateGyroVisuals();
}

QSize RobDisplayWidget_t::minimumSizeHint(void) const
{
	return QSize(630, 277);
}

QSize RobDisplayWidget_t::sizeHint(void) const
{
	return QSize(945, 416);
}

void RobDisplayWidget_t::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	painter.fillRect(rect(), Qt::white);

	beginScene(painter, size());

	if (snapshot.game == FCEU_ROB_NONE) {
		drawROB(painter, 630.0, 10.0, false);
	} else if (snapshot.game == FCEU_ROB_STACK_UP) {
		drawStackUpScene(painter, snapshot);
	} else {
		drawGyromiteScene(painter, snapshot, gyros);
	}
}

void RobDisplayWidget_t::updateGyroVisuals(void)
{
	if (snapshot.game != FCEU_ROB_GYROMITE) {
		for (int i = 0; i < 2; ++i) {
			gyros[i].wobbleTick = 0;
		}
		return;
	}

	for (int i = 0; i < 2; ++i) {
		if (snapshot.gyroTimers[i] > 0 && snapshot.gyroTimers[i] <= GYRO_WARNING_FRAMES) {
			++gyros[i].wobbleTick;
		} else {
			gyros[i].wobbleTick = 0;
		}
	}
}

RobWindowDialog_t::RobWindowDialog_t(QWidget* parent)
	: QDialog(parent, Qt::Window)
{
	QSettings settings;

	robWindow = this;

	setWindowTitle(tr("R.O.B. Viewer"));
	setWindowFlag(Qt::WindowMaximizeButtonHint, true);

	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	display = new RobDisplayWidget_t(this);
	mainLayout->addWidget(display, 1);
	setLayout(mainLayout);

	QShortcut* toggleShortcut = new QShortcut(QKeySequence(Qt::Key_B), this);
	connect(toggleShortcut, &QShortcut::activated, this, [this]() { close(); });

	updateTimer = new QTimer(this);
	connect(updateTimer, &QTimer::timeout, this, &RobWindowDialog_t::periodicUpdate);
	updateTimer->start(FRAME_MS);

	periodicUpdate();
	resize(sizeHint());

	const QRect savedGeometry = settings.value("robViewer/geometryRect").toRect();
	if (geometryVisibleOnAnyScreen(savedGeometry)) {
		setGeometry(savedGeometry);
	} else {
		setGeometry(defaultRobWindowGeometry(parentWidget(), sizeHint()));
	}
}

RobWindowDialog_t::~RobWindowDialog_t(void)
{
	QSettings settings;

	updateTimer->stop();
	robWindow = nullptr;
	settings.setValue("robViewer/geometryRect", geometry());
}

void RobWindowDialog_t::closeEvent(QCloseEvent* event)
{
	done(0);
	deleteLater();
	event->accept();
}

void RobWindowDialog_t::periodicUpdate(void)
{
	FCEURobSnapshot snapshotValue;

	std::memset(&snapshotValue, 0, sizeof(snapshotValue));
	FCEU_RobGetSnapshot(&snapshotValue);
	display->setSnapshot(snapshotValue);
	display->tickVisuals();
	display->update();
}

int openRobWindow(QWidget* parent)
{
	if (robWindow != nullptr) {
		robWindow->activateWindow();
		robWindow->raise();
		return -1;
	}

	robWindow = new RobWindowDialog_t(parent);
	robWindow->setAttribute(Qt::WA_ShowWithoutActivating, true);
	robWindow->show();
	if (parent != nullptr) {
		parent->activateWindow();
		parent->raise();
	}

	return 0;
}

int toggleRobWindow(QWidget* parent)
{
	if (robWindow != nullptr) {
		robWindow->close();
		return 1;
	}
	return openRobWindow(parent);
}

bool robWindowIsOpen(void)
{
	return robWindow != nullptr;
}

void closeRobWindow(void)
{
	if (robWindow != nullptr) {
		robWindow->close();
	}
}
