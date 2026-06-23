#pragma once

// qbar crash guard.
//
// Installs a SIGSEGV handler that does two things, in this order:
//   1. Logs a symbolized traceback for EVERY fault (no "ostrich algorithm" — we always
//      record what happened), using glibc dladdr/backtrace. No gdb, no libunwind, nothing
//      the user has to install.
//   2. Recovers from ONE specific, benign Qt teardown race and lets the process keep
//      running: a Wayland frame callback delivered after the window's shell surface was
//      already freed. The path is
//        QWaylandWindow::doHandleFrameCallback() -> updateExposure()
//          -> isExposed() on a dangling mShellSurface -> call through a null vtable slot.
//      The frame callback is only a render-throttle for a window that is being destroyed,
//      so dropping it is safe; we unwind doHandleFrameCallback and resume in its caller.
//
// Any fault that is NOT that exact signature is logged and then re-raised through the
// previous (default) handler, so genuine bugs still abort and dump core as before.
void installCrashGuard();

// Number of times the frame-callback teardown race has been intercepted and recovered.
unsigned crashGuardRecoveredCount();
