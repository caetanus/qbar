#define _GNU_SOURCE 1

#include "crashguard.h"

#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>

#if defined(__linux__) && defined(__x86_64__)
#include <dlfcn.h>
#include <link.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

namespace {

// Address range of QtWaylandClient::QWaylandWindow::doHandleFrameCallback(), resolved at
// install time. The recoverable fault always returns into this function.
uintptr_t g_dhfcStart = 0;
uintptr_t g_dhfcEnd = 0;
// The exposure functions doHandleFrameCallback calls. The teardown race faults in two ways:
// rip==0 (a call through the freed mShellSurface's null vtable slot) OR a fault INSIDE these
// (dereferencing the freed surface's members directly). Both unwind to doHandleFrameCallback.
uintptr_t g_updExpStart = 0, g_updExpEnd = 0;   // QWaylandWindow::updateExposure
uintptr_t g_calcExpStart = 0, g_calcExpEnd = 0; // QWaylandWindow::calculateExposure
// Only attempt recovery if we recognised doHandleFrameCallback's prologue, so we know the
// exact stack layout to emulate its epilogue. If Qt is built differently, we degrade to
// log-and-re-raise instead of mangling the context blindly.
bool g_recoveryArmed = false;
struct sigaction g_prevSegv;
std::atomic<unsigned> g_recoveredCount{0};

// ---- async-signal-safe output helpers (no malloc, no stdio) ----
void rawWrite(const char *s) { (void)::write(STDERR_FILENO, s, std::strlen(s)); }

void rawWriteHex(uintptr_t v)
{
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        const int nyb = static_cast<int>((v >> ((15 - i) * 4)) & 0xf);
        buf[2 + i] = static_cast<char>(nyb < 10 ? ('0' + nyb) : ('a' + nyb - 10));
    }
    buf[18] = '\0';
    rawWrite(buf);
}

void rawWriteDec(unsigned v)
{
    char b[12];
    int i = 12;
    b[--i] = '\0';
    if (v == 0) {
        b[--i] = '0';
    }
    while (v != 0) {
        b[--i] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    rawWrite(&b[i]);
}

bool readable(uintptr_t p) { return p > 0x1000 && (p & 0x7) == 0; }

// One symbolized backtrace line: "  #N  0x.... (lib symbol)". dladdr is not strictly
// async-signal-safe, but it is the standard, pragmatic choice in a crash handler.
void logFrame(int idx, uintptr_t addr)
{
    rawWrite("  #");
    rawWriteDec(static_cast<unsigned>(idx));
    rawWrite(idx < 10 ? "  " : " ");
    rawWriteHex(addr);
    Dl_info di;
    if (addr != 0 && dladdr(reinterpret_cast<void *>(addr), &di) != 0) {
        if (di.dli_fname != nullptr) {
            rawWrite("  ");
            rawWrite(di.dli_fname);
        }
        if (di.dli_sname != nullptr) {
            rawWrite("  ");
            // Demangle the C++ symbol (libstdc++'s __cxa_demangle — no need to shell out to
            // c++filt). It mallocs, which isn't async-signal-safe, but these faults are vtable/
            // stack races, not heap corruption, so it's the pragmatic choice in a crash handler.
            int status = 0;
            char *demangled = abi::__cxa_demangle(di.dli_sname, nullptr, nullptr, &status);
            if (status == 0 && demangled != nullptr) {
                rawWrite(demangled);
                std::free(demangled);
            } else {
                rawWrite(di.dli_sname);
            }
        }
    }
    rawWrite("\n");
}

// Walk the frame-pointer chain from the faulting context and log it. rip==0 (a call through
// a null pointer) loses frame #0, so we also surface [rsp] — the return address the null
// call pushed — which names the function that made the bad call.
void logTraceback(uintptr_t rip, uintptr_t rsp, uintptr_t rbp)
{
    int idx = 0;
    logFrame(idx++, rip);
    if (readable(rsp)) {
        logFrame(idx++, *reinterpret_cast<uintptr_t *>(rsp));
    }
    uintptr_t fp = rbp;
    for (int d = 0; d < 64 && readable(fp); ++d) {
        const uintptr_t ret = *reinterpret_cast<uintptr_t *>(fp + 8);
        if (ret == 0) {
            break;
        }
        logFrame(idx++, ret);
        const uintptr_t next = *reinterpret_cast<uintptr_t *>(fp);
        if (next <= fp) {
            break; // chain must ascend; stop on anything suspicious
        }
        fp = next;
    }
}

void handler(int sig, siginfo_t * /*info*/, void *ucv)
{
    auto *uc = static_cast<ucontext_t *>(ucv);
    const uintptr_t rip = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    const uintptr_t rsp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
    const uintptr_t rbp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);

    rawWrite("\n=== qbar crash guard: SIGSEGV (rip=");
    rawWriteHex(rip);
    rawWrite(") ===\n");
    logTraceback(rip, rsp, rbp);

    // Recoverable signature: a frame callback delivered to a window whose shell surface was
    // already freed. It faults two ways — rip==0 (call through the freed mShellSurface's null
    // vtable slot) OR a fault INSIDE updateExposure/calculateExposure (touching the freed
    // surface's members). Both have doHandleFrameCallback live in the frame chain; we unwind it
    // and resume in its caller as if the (no-op, render-throttle) callback had completed.
    const bool inExposurePath = rip == 0
        || (g_updExpStart != 0 && rip >= g_updExpStart && rip < g_updExpEnd)
        || (g_calcExpStart != 0 && rip >= g_calcExpStart && rip < g_calcExpEnd);
    if (g_recoveryArmed && inExposurePath) {
        uintptr_t fp = rbp;
        for (int d = 0; d < 32 && readable(fp); ++d) {
            const uintptr_t ret = *reinterpret_cast<uintptr_t *>(fp + 8);
            if (ret >= g_dhfcStart && ret < g_dhfcEnd) {
                // `fp` is doHandleFrameCallback's callee; its saved rbp IS dhfc's rbp.
                const uintptr_t dhfcRbp = *reinterpret_cast<uintptr_t *>(fp);
                if (!readable(dhfcRbp)) {
                    break;
                }
                // Emulate dhfc's epilogue (prologue was: push rbp; mov rsp,rbp; push r12;
                // push rbx) → pop rbx; pop r12; pop rbp; ret.
                uc->uc_mcontext.gregs[REG_RBX] = static_cast<greg_t>(*reinterpret_cast<uintptr_t *>(dhfcRbp - 16));
                uc->uc_mcontext.gregs[REG_R12] = static_cast<greg_t>(*reinterpret_cast<uintptr_t *>(dhfcRbp - 8));
                uc->uc_mcontext.gregs[REG_RBP] = static_cast<greg_t>(*reinterpret_cast<uintptr_t *>(dhfcRbp));
                uc->uc_mcontext.gregs[REG_RIP] = static_cast<greg_t>(*reinterpret_cast<uintptr_t *>(dhfcRbp + 8));
                uc->uc_mcontext.gregs[REG_RSP] = static_cast<greg_t>(dhfcRbp + 16);
                g_recoveredCount.fetch_add(1, std::memory_order_relaxed);
                rawWrite("  --> recovered: stale Wayland frame callback dropped, resuming.\n\n");
                return;
            }
            const uintptr_t next = *reinterpret_cast<uintptr_t *>(fp);
            if (next <= fp) {
                break;
            }
            fp = next;
        }
        rawWrite("  --> exposure-path fault but doHandleFrameCallback not in chain; not recovering.\n");
    }

    // Anything else: restore the previous handler and let it run (default → core dump), so
    // genuine bugs are not masked.
    rawWrite("=== unrecoverable; re-raising ===\n");
    ::sigaction(sig, &g_prevSegv, nullptr);
    // Returning re-executes the faulting instruction under the restored handler.
}

// Resolve a libQt6WaylandClient symbol's [start,end) address range (0 if unavailable). Qt loads
// the lib RTLD_LOCAL, so RTLD_DEFAULT can miss it — grab the already-loaded lib via RTLD_NOLOAD.
void resolveSymbolRange(const char *mangled, uintptr_t &start, uintptr_t &end)
{
    void *sym = nullptr;
    for (const char *soname : {"libQt6WaylandClient.so.6", "libQt6WaylandClient.so"}) {
        if (void *lib = dlopen(soname, RTLD_NOLOAD | RTLD_NOW)) {
            sym = dlsym(lib, mangled);
            if (sym != nullptr) {
                break;
            }
        }
    }
    if (sym == nullptr) {
        sym = dlsym(RTLD_DEFAULT, mangled);
    }
    if (sym == nullptr) {
        start = 0;
        end = 0;
        return;
    }
    start = reinterpret_cast<uintptr_t>(sym);
    Dl_info info;
    void *extra = nullptr;
    if (dladdr1(sym, &info, &extra, RTLD_DL_SYMENT) != 0 && extra != nullptr) {
        end = start + static_cast<const ElfW(Sym) *>(extra)->st_size;
    } else {
        end = start + 0x400;
    }
}

// Find doHandleFrameCallback, record its [start,end), and verify its prologue so recovery is
// only armed when the stack layout we emulate actually matches this Qt build.
void resolveTarget()
{
    // Qt loads the platform plugin (and its libQt6WaylandClient dependency) with RTLD_LOCAL,
    // so the symbol is NOT in the global scope RTLD_DEFAULT searches. Grab a handle to the
    // already-loaded library (RTLD_NOLOAD never loads a fresh copy) and look it up there.
    static const char *kSym = "_ZN15QtWaylandClient14QWaylandWindow21doHandleFrameCallbackEv";
    void *sym = nullptr;
    for (const char *soname : {"libQt6WaylandClient.so.6", "libQt6WaylandClient.so"}) {
        if (void *lib = dlopen(soname, RTLD_NOLOAD | RTLD_NOW)) {
            sym = dlsym(lib, kSym);
            if (sym != nullptr) {
                break;
            }
        }
    }
    if (sym == nullptr) {
        sym = dlsym(RTLD_DEFAULT, kSym); // last resort
    }
    if (sym == nullptr) {
        return; // not a Wayland build / symbol unavailable → no recovery, just logging
    }
    g_dhfcStart = reinterpret_cast<uintptr_t>(sym);

    Dl_info info;
    void *extra = nullptr;
    if (dladdr1(sym, &info, &extra, RTLD_DL_SYMENT) != 0 && extra != nullptr) {
        const auto *e = static_cast<const ElfW(Sym) *>(extra);
        g_dhfcEnd = g_dhfcStart + e->st_size;
    } else {
        g_dhfcEnd = g_dhfcStart + 0x200; // conservative fallback
    }

    // Prologue: endbr64 (f3 0f 1e fa); push rbp (55); ...; mov rsp,rbp (48 89 e5);
    // push r12 (41 54); push rbx (53). Confirm structure → [rbp-8]=r12, [rbp-16]=rbx.
    const auto *p = static_cast<const unsigned char *>(sym);
    bool ok = p[0] == 0xf3 && p[1] == 0x0f && p[2] == 0x1e && p[3] == 0xfa && p[4] == 0x55;
    if (ok) {
        bool movRbp = false;
        bool pushR12 = false;
        bool pushRbx = false;
        for (int i = 5; i < 28; ++i) {
            if (!movRbp && p[i] == 0x48 && p[i + 1] == 0x89 && p[i + 2] == 0xe5) {
                movRbp = true;
                i += 2;
            } else if (movRbp && !pushR12 && p[i] == 0x41 && p[i + 1] == 0x54) {
                pushR12 = true;
                i += 1;
            } else if (pushR12 && !pushRbx && p[i] == 0x53) {
                pushRbx = true;
                break;
            }
        }
        ok = movRbp && pushR12 && pushRbx;
    }
    g_recoveryArmed = ok;

    // The exposure functions, so the handler can recognise the rip!=0 fault variant (a crash
    // INSIDE the exposure computation, not just the null vtable call).
    resolveSymbolRange("_ZN15QtWaylandClient14QWaylandWindow14updateExposureEv", g_updExpStart, g_updExpEnd);
    resolveSymbolRange("_ZNK15QtWaylandClient14QWaylandWindow17calculateExposureEv", g_calcExpStart, g_calcExpEnd);
}

} // namespace

void installCrashGuard()
{
    resolveTarget();

    // Faults at rip==0 can leave the stack in an awkward state; run the handler on its own
    // stack to be safe.
    static char altStack[65536];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    ss.ss_flags = 0;
    ::sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    ::sigemptyset(&sa.sa_mask);
    ::sigaction(SIGSEGV, &sa, &g_prevSegv);

    if (g_recoveryArmed) {
        qInfo("crash guard: armed (recovers Wayland frame-callback teardown race; range %p..%p)",
              reinterpret_cast<void *>(g_dhfcStart), reinterpret_cast<void *>(g_dhfcEnd));
    } else if (g_dhfcStart == 0) {
        qInfo("crash guard: traceback-only (doHandleFrameCallback symbol not found)");
    } else {
        qInfo("crash guard: traceback-only (doHandleFrameCallback prologue not recognised at %p)",
              reinterpret_cast<void *>(g_dhfcStart));
    }
}

unsigned crashGuardRecoveredCount()
{
    return g_recoveredCount.load(std::memory_order_relaxed);
}

#else // not Linux/x86_64: no-op guard

void installCrashGuard() {}
unsigned crashGuardRecoveredCount() { return 0; }

#endif
