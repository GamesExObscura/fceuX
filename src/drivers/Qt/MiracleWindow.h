#pragma once

#include <QDialog>
#include <QHash>
#include <QTouchEvent>
#include <QWidget>

#include "../../input/miracle.h"

class QPaintEvent;
class QMouseEvent;
class QResizeEvent;
class QTimer;
class QLabel;
class QSlider;
class MiraclePianoDialog_t;

class MiraclePianoWidget_t : public QWidget
{
	Q_OBJECT

	public:
		MiraclePianoWidget_t(QWidget* parent = 0);

		void getState(FCEUMiraclePianoInputState* out) const;
		void peekState(FCEUMiraclePianoInputState* out) const;

		QSize minimumSizeHint(void) const override;
		QSize sizeHint(void) const override;

	protected:
		bool event(QEvent* event) override;
		void paintEvent(QPaintEvent* event) override;
		void resizeEvent(QResizeEvent* event) override;
		void mousePressEvent(QMouseEvent* event) override;
		void mouseMoveEvent(QMouseEvent* event) override;
		void mouseReleaseEvent(QMouseEvent* event) override;
		void leaveEvent(QEvent* event) override;

	private:
		void updatePreview(void) const;

		enum ElementType
		{
			ElementNone = 0,
			ElementWhiteKey,
			ElementBlackKey,
			ElementButton,
			ElementPedal
		};

		struct HitResult
		{
			ElementType type;
			int index;
		};

		struct PanelButton
		{
			QRectF labelRect;
			QRectF buttonRect;
			QString label;
		};

		void updateLayout(void);
		bool isBlackKey(int noteIndex) const;
		int whiteKeyIndex(int noteIndex) const;
		HitResult hitTest(const QPointF& point) const;
		void setMouseHit(const HitResult& hit);
		void clearMouseHit(void);
		void applyHitState(const HitResult& hit, bool pressed);
		void rebuildTouchState(QTouchEvent* event);
		void togglePedal(void);

		QRectF whiteKeys[FCEU_MIRACLE_NUM_KEYS];
		QRectF blackKeys[FCEU_MIRACLE_NUM_KEYS];
		PanelButton panelButtons[FCEU_MIRACLE_NUM_BUTTONS];
		QRectF pedalLabelRect;
		QRectF pedalRect;

		bool keyPressed[FCEU_MIRACLE_NUM_KEYS];
		bool buttonPressed[FCEU_MIRACLE_NUM_BUTTONS];
		bool pedalPressed;
		HitResult mouseHit;
		bool mousePressed;
		QHash<int, HitResult> touchHits;
};

class MiraclePianoDialog_t : public QDialog
{
	Q_OBJECT

	public:
		MiraclePianoDialog_t(QWidget* parent = 0);
		~MiraclePianoDialog_t(void);

		void getState(FCEUMiraclePianoInputState* out) const;

	protected:
		bool eventFilter(QObject* watched, QEvent* event) override;
		void closeEvent(QCloseEvent* event) override;

	private:
		void periodicUpdate(void);
		void scheduleFocusReturn(void);
		void returnFocusToGame(void);
		void updateVolumeLabel(void);

		MiraclePianoWidget_t* piano;
		QSlider* volumeSlider;
		QLabel* volumeValue;
		QTimer* updateTimer;
		QTimer* focusReturnTimer;
		QWidget* focusReturnTarget;
};

int openMiraclePianoWindow(QWidget* parent);
void initMiraclePianoAudio();
void setMiraclePianoAudioVolume(int percent);
void mixMiraclePianoAudio(int32* samples, int count, int rate);
void resetMiraclePianoAudio();
void shutdownMiraclePianoAudio();
int toggleMiraclePianoWindow(QWidget* parent);
bool miraclePianoWindowIsOpen(void);
void closeMiraclePianoWindow(void);
void getMiraclePianoVirtualState(FCEUMiraclePianoInputState* out);
