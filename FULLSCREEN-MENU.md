# Fullscreen menus

Fullscreen automatically hides the menu. Move the mouse to the top four pixels
of the game window to reveal it. Dropdowns and keyboard menu navigation keep it
visible; moving away hides it after 650 ms. Exiting fullscreen restores the bar.
The separate Auto Hide Menu on Fullscreen option was removed. Windowed Hide Menu
remains available.

The follow-up dropdown fix uses QObject ownership rather than QWidget ancestry:
Qt popup windows break the QWidget ancestor chain. Both popup retention and native
handling now recognize FCEUX-owned menus. Activation changes no longer trigger
immediate hiding during popup creation.

The build and offline regression checks passed, including an offscreen Qt test
that opens real QMenu dropdowns/submenus and retains them past the hide timeout.
The additional native Windows test uses a continuously updating QOpenGLWidget,
reveals the menu from the top edge, clicks it, and verifies native popup placement.
The user's trace showed native menus visible, correctly positioned and above the
main window despite invisible dropdowns on screen. The WS_BORDER workaround did
not resolve this on the user's system. Windows now uses a borderless normal
window instead of Qt fullscreen, leaving one logical pixel at the bottom so the
OpenGL surface does not occupy the whole display. Native window position, size
and maximized state are preserved when leaving this view. Non-Windows platforms
keep the existing Qt fullscreen implementation.

The native test also checks restored geometry, maximized-state restoration, and
that the OpenGL viewport remains valid after repeated transitions. The previous
style-bit and topmost-popup workarounds are no longer used in FCEUX.

Reference: https://doc.qt.io/qt-6/windows-issues.html#fullscreen-opengl-based-windows

Native and offscreen tests passed. No FCEUX instance or ROM was launched. The
actual emulator still needs user verification with its renderer and game running.

Optional tracing: run Diagnose fullscreen menus.cmd beside the executable.
It sets FCEUX_MENU_TRACE for that process only and records fullscreen-menu-trace.txt.
Normal launches do not record a trace. Existing user traces are not deleted.
