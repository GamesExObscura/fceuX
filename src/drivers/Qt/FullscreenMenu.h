#pragma once

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QScreen>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

inline bool fullscreenPresentationActive(const QMainWindow* window)
{
    return window->isFullScreen() || window->property("fceuxBorderlessFullscreen").toBool();
}

inline void setMenuFriendlyFullscreen(QMainWindow* window, bool enabled)
{
#ifdef Q_OS_WIN
    if (enabled == fullscreenPresentationActive(window)) return;
    if (enabled) {
        QScreen* screen = window->screen();
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        window->setProperty("fceuxWindowedGeometry", window->isMaximized() ? window->normalGeometry() : window->geometry());
        window->setProperty("fceuxWindowedFlags", int(window->windowFlags()));
        window->setProperty("fceuxWindowedPlacement", window->saveGeometry());
        window->setProperty("fceuxWindowedMaximized", window->isMaximized());
        RECT nativeFrame = {};
        GetWindowRect(reinterpret_cast<HWND>(window->winId()), &nativeFrame);
        window->setProperty("fceuxNativeWindowedFrame", QRect(nativeFrame.left, nativeFrame.top,
            nativeFrame.right - nativeFrame.left, nativeFrame.bottom - nativeFrame.top));
        window->setProperty("fceuxBorderlessFullscreen", true);
        window->menuBar()->hide();
        window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
        window->showNormal();
        // A genuinely smaller surface prevents OpenGL direct scanout. A style
        // bit alone still left popups invisible on the user's Windows driver.
        window->setGeometry(screen->geometry().adjusted(0, 0, 0, -1));
        window->raise();
        window->activateWindow();
    } else {
        const QRect geometry = window->property("fceuxWindowedGeometry").toRect();
        const bool maximized = window->property("fceuxWindowedMaximized").toBool();
        window->setProperty("fceuxBorderlessFullscreen", false);
        window->setWindowFlags(Qt::WindowFlags(window->property("fceuxWindowedFlags").toInt()));
        window->showNormal();
        window->menuBar()->show();
        if (!window->restoreGeometry(window->property("fceuxWindowedPlacement").toByteArray()) && geometry.isValid())
            window->setGeometry(geometry);
        if (maximized) window->showMaximized();
        else {
            // Qt can retain frameless client margins while restoring decorations.
            // Restore the saved native outer rectangle to avoid position/size drift.
            const QRect frame = window->property("fceuxNativeWindowedFrame").toRect();
            if (frame.isValid()) SetWindowPos(reinterpret_cast<HWND>(window->winId()), nullptr,
                frame.x(), frame.y(), frame.width(), frame.height(),
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        window->activateWindow();
    }
#else
    if (enabled) window->showFullScreen();
    else window->showNormal();
#endif
}

inline bool fullscreenMenuBelongsToWindow(const QObject* popup, const QObject* window)
{
    // QWidget::isAncestorOf stops at a window boundary; QMenu is a popup window.
    for (auto owner = popup; owner; owner = owner->parent())
        if (owner == window) return true;
    return false;
}

inline bool keepFullscreenMenuVisible(bool popup, bool keyboardFocus, bool overBar,
    bool active, qint64 millisecondsAway)
{
    return popup || keyboardFocus || (active && (overBar || millisecondsAway < 650));
}

class FullscreenMenuController final : public QObject
{
public:
    FullscreenMenuController(QMainWindow* window, QMenuBar* bar)
        : QObject(window), window(window), bar(bar)
    {
        idle.start();
        const QString tracePath = qEnvironmentVariable("FCEUX_MENU_TRACE");
        if (!tracePath.isEmpty()) {
            trace.setFileName(tracePath);
            if (trace.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                snapshot("trace-start");
        }
        qApp->installEventFilter(this);
        auto timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { updateVisibility(); });
        timer->start(50);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (trace.isOpen() && fullscreenMenuBelongsToWindow(watched, window)) {
            const auto type = event->type();
            const bool click = type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease;
            const bool menuEvent = (watched == bar || qobject_cast<QMenu*>(watched))
                && (type == QEvent::Show || type == QEvent::Hide);
            if (click || menuEvent || (watched == window && type == QEvent::WindowStateChange)) {
                snapshot(QString("event=%1 target=%2").arg(int(type)).arg(watched->metaObject()->className()));
                if (click && type == QEvent::MouseButtonPress) {
                    for (int delay : {0, 150, 700})
                        QTimer::singleShot(delay, this, [this, delay] {
                            snapshot(QString("after-click-%1ms").arg(delay));
                        });
                }
            }
        }
        if (watched == window && (event->type() == QEvent::WindowStateChange
            || event->type() == QEvent::DynamicPropertyChange)
            && fullscreenPresentationActive(window) != wasFullscreen)
            updateVisibility();
        if (watched == bar && event->type() == QEvent::Show) idle.restart();
        if (event->type() == QEvent::Show)
        {
            auto menu = qobject_cast<QMenu*>(watched);
            if (menu && fullscreenMenuBelongsToWindow(menu, window))
            {
                idle.restart();
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void snapshot(const QString& reason)
    {
        if (!trace.isOpen() || traceRecords++ >= 2000) return;
        QTextStream out(&trace);
        auto rectText = [](const QRect& r) {
            return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        };
        QWidget* popup = QApplication::activePopupWidget();
        out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " " << reason
            << " Qt=" << qVersion() << " platform=" << QGuiApplication::platformName()
            << " fullscreen=" << fullscreenPresentationActive(window) << " qtFullscreen=" << window->isFullScreen() << " active=" << window->isActiveWindow()
            << " bar=" << bar->isVisible() << " barFocus=" << bar->hasFocus()
            << " frame=" << rectText(window->frameGeometry())
            << " barRect=" << rectText(QRect(bar->mapToGlobal(QPoint()), bar->size()))
            << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y()
            << " popup=" << (popup ? popup->metaObject()->className() : "none") << "\n";
#ifdef Q_OS_WIN
        const HWND main = reinterpret_cast<HWND>(window->internalWinId());
        out << "  mainStyle=" << QString::number(GetWindowLongPtr(main, GWL_STYLE), 16)
            << " foreground=" << quintptr(GetForegroundWindow()) << " main=" << quintptr(main) << "\n";
#endif
        for (auto menu : window->findChildren<QMenu*>()) {
            if (!menu->isVisible()) continue;
            out << "  menu=" << menu->title() << " geometry=" << rectText(menu->geometry())
                << " actions=" << menu->actions().size() << "\n";
#ifdef Q_OS_WIN
            const HWND handle = reinterpret_cast<HWND>(menu->internalWinId());
            RECT r = {};
            GetWindowRect(handle, &r);
            POINT point = {r.left + 10, r.top + 10};
            out << "    nativeVisible=" << IsWindowVisible(handle) << " hwnd=" << quintptr(handle)
                << " windowAtMenu=" << quintptr(WindowFromPoint(point))
                << " nativeRect=" << r.left << "," << r.top << "," << r.right << "," << r.bottom << "\n";
#endif
        }
        out.flush();
        trace.flush();
    }

    void updateVisibility()
    {
        const bool full = fullscreenPresentationActive(window);
        if (full != wasFullscreen)
        {
            wasFullscreen = full;
            bar->setVisible(!full);
            idle.restart();
            return;
        }
        if (!full) return;

        const QPoint cursor = QCursor::pos();
        const QRect frame = window->frameGeometry();
        const bool inside = frame.contains(cursor);
        const bool active = window->isActiveWindow();
        if (!bar->isVisible())
        {
            if (active && inside && cursor.y() < frame.top() + 4)
            {
                bar->show();
                bar->raise();
                idle.restart();
            }
            return;
        }
        const bool overBar = bar->rect().adjusted(0, 0, 0, 8).contains(bar->mapFromGlobal(QCursor::pos()));
        QWidget* popup = QApplication::activePopupWidget();
        bool usingPopup = popup && fullscreenMenuBelongsToWindow(popup, window);
        // Cover the brief transition between a parent dropdown and its submenu.
        if (!usingPopup)
            for (auto menu : window->findChildren<QMenu*>())
                if (menu->isVisible()) { usingPopup = true; break; }
        if (usingPopup || bar->hasFocus() || (active && overBar)) idle.restart();
        if (!keepFullscreenMenuVisible(usingPopup, bar->hasFocus(), overBar, active, idle.elapsed()))
            bar->hide();
    }

    QMainWindow* window;
    QMenuBar* bar;
    QElapsedTimer idle;
    bool wasFullscreen = false;
    QFile trace;
    int traceRecords = 0;
};
