// RobWindow.h
//

#pragma once

#include <QCloseEvent>
#include <QDialog>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "../../input.h"

class QPaintEvent;

class RobDisplayWidget_t : public QWidget
{
	public:
		struct GyroVisualState
		{
			int wobbleTick;
		};

		RobDisplayWidget_t(QWidget* parent = 0);

		void setSnapshot(const FCEURobSnapshot& value);
		void tickVisuals(void);

		QSize minimumSizeHint(void) const override;
		QSize sizeHint(void) const override;

	protected:
		void paintEvent(QPaintEvent* event) override;

	private:
		void updateGyroVisuals(void);

		FCEURobSnapshot snapshot;
		GyroVisualState gyros[2];
};

class RobWindowDialog_t : public QDialog
{
	Q_OBJECT

	public:
		RobWindowDialog_t(QWidget* parent = 0);
		~RobWindowDialog_t(void);

	protected:
		void closeEvent(QCloseEvent* event) override;

		RobDisplayWidget_t* display;
		QTimer* updateTimer;

	private slots:
		void periodicUpdate(void);
};

int openRobWindow(QWidget* parent);
int toggleRobWindow(QWidget* parent);
bool robWindowIsOpen(void);
void closeRobWindow(void);
