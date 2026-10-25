#include <cstdio>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include "drivers/Qt/FullscreenMenu.h"

class ThemeCheck : public QObject {
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::Polish
            || event->type() == QEvent::PolishRequest) {
            if (auto menu = qobject_cast<QMenu*>(obj))
                menu->setStyleSheet("QMenu { background: white; color: black; border: 1px solid black; } QMenu::item { padding: 4px 24px; }");
        }
        return false;
    }
};

int main(int argc, char** argv)
{
    const bool native = argc > 1;
    qputenv("QT_QPA_PLATFORM", native ? "windows" : "offscreen");
    QApplication app(argc, argv);
    const QPoint previousCursor = QCursor::pos();
    ThemeCheck theme;
    app.installEventFilter(&theme);
    struct Case { bool popup, keyboard, overBar, active; qint64 elapsed; bool visible; };
    const Case cases[] = {
        {false, false, true, true, 1000, true},
        {true, false, false, false, 1000, true}, // A popup temporarily owns activation.
        {false, true, false, true, 1000, true},
        {false, false, false, true, 649, true},
        {false, false, false, true, 650, false},
        {false, false, false, false, 100, false},
        {false, false, true, false, 1000, false},
    };
    for (const auto& c : cases)
        if (keepFullscreenMenuVisible(c.popup, c.keyboard, c.overBar, c.active, c.elapsed) != c.visible)
            return 1;
    std::puts("PASS: fullscreen menu stays accessible for dropdowns and keyboard navigation; hide delay respects activation");
    QMainWindow window;
    if (native) {
        auto gl = new QOpenGLWidget(&window);
        window.setCentralWidget(gl);
        auto redraw = new QTimer(gl);
        QObject::connect(redraw, &QTimer::timeout, gl, [gl] { gl->update(); });
        redraw->start(16);
    }
    auto bar = window.menuBar();
    auto menu = bar->addMenu("File");
    menu->addAction("Open");
    auto submenu = menu->addMenu("Recent");
    submenu->addAction("Example");
    QMainWindow unrelated;
    if (!fullscreenMenuBelongsToWindow(menu, &window)
        || !fullscreenMenuBelongsToWindow(submenu, &window)
        || fullscreenMenuBelongsToWindow(menu, &unrelated)) return 3;
    new FullscreenMenuController(&window, bar);
    window.setGeometry(80, 80, 800, 600);
    window.show();
    app.processEvents();
    const QRect windowed = window.geometry();
    setMenuFriendlyFullscreen(&window, true);
    app.processEvents();
    bar->show();
    window.activateWindow();
    window.raise();
    if (native) {
        bar->hide();
        QCursor::setPos(window.frameGeometry().topLeft() + QPoint(100, 0));
        QTimer::singleShot(500, &app, [&] {
            if (!bar->isVisible()) {
                const auto frame = window.frameGeometry();
                const auto cursor = QCursor::pos();
                std::printf("Reveal failure: active=%d full=%d frame=%d,%d,%d,%d cursor=%d,%d\n", window.isActiveWindow(), window.isFullScreen(), frame.x(), frame.y(), frame.width(), frame.height(), cursor.x(), cursor.y());
                QCursor::setPos(previousCursor);
                app.exit(5); return;
            }
            const QPoint local = bar->actionGeometry(menu->menuAction()).center();
            const QPoint global = bar->mapToGlobal(local);
            QCursor::setPos(global);
            QMouseEvent down(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            app.sendEvent(bar, &down);
            app.sendEvent(bar, &up);
        });
        QTimer::singleShot(900, &app, [&] {
            std::printf("Native click: bar=%d menu=%d activePopup=%p\n", bar->isVisible(), menu->isVisible(), QApplication::activePopupWidget());
            const auto r = menu->geometry();
            POINT point = {r.x() + 10, r.y() + 10};
            std::printf("Menu geometry=%d,%d,%d,%d HWND=%p windowAtPoint=%p\n", r.x(), r.y(), r.width(), r.height(), reinterpret_cast<HWND>(menu->winId()), WindowFromPoint(point));
            const bool composed = fullscreenPresentationActive(&window) && !window.isFullScreen()
                && window.height() < window.screen()->geometry().height();
            std::printf("Composited borderless view active: %d\n", composed);
            bool passed = menu->isVisible() && composed;
            menu->hide();
            setMenuFriendlyFullscreen(&window, false);
            app.processEvents();
            const bool restored = !fullscreenPresentationActive(&window) && bar->isVisible()
                && window.geometry() == windowed;
            std::printf("Windowed geometry/menu restored: %d\n", restored);
            const auto actual = window.geometry();
            std::printf("Geometry expected=%d,%d,%d,%d actual=%d,%d,%d,%d bar=%d full=%d\n", windowed.x(), windowed.y(), windowed.width(), windowed.height(), actual.x(), actual.y(), actual.width(), actual.height(), bar->isVisible(), fullscreenPresentationActive(&window));
            passed = passed && restored;
            window.showMaximized();
            app.processEvents();
            setMenuFriendlyFullscreen(&window, true);
            app.processEvents();
            setMenuFriendlyFullscreen(&window, false);
            app.processEvents();
            std::printf("Maximized state restored: %d\n", window.isMaximized());
            passed = passed && window.isMaximized();
            const bool glValid = window.findChild<QOpenGLWidget*>()->isValid();
            std::printf("OpenGL viewport survives toggles: %d\n", glValid);
            passed = passed && glValid;
            QCursor::setPos(previousCursor);
            app.exit(passed ? 0 : 4);
        });
        return app.exec();
    }
    menu->popup(bar->mapToGlobal(QPoint(0, bar->height())));
    std::printf("Popup ownership: widget ancestor=%d, QObject parent=%d\n",
        window.isAncestorOf(menu), menu->parent() == bar);
    QTimer::singleShot(100, &app, [&] {
        submenu->popup(menu->mapToGlobal(QPoint(menu->width(), 0)));
    });
    QTimer::singleShot(800, &app, [&] {
        const bool visible = menu->isVisible() && submenu->isVisible() && bar->isVisible();
        std::printf("Popup remains visible after event processing: %d\n", visible);
        app.exit(visible ? 0 : 2);
    });
    return app.exec();
}
