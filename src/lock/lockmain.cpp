#include "lockcontroller.h"
#include "pamauthenticator.h"
#include "waylandlockbackend.h"
#include "x11lockbackend.h"
#include "qmlcss/csstheme.h"

using QmlCss::CssTheme;
#include "platform/capslockmonitor.h"
#include "user/usermodel.h"

#include <QCommandLineParser>
#include <QCursor>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlContext>
#include <QQuickView>
#include <QHash>
#include <QScreen>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>
#include <QTranslator>

#include <cstdio>
#include <memory>

namespace {

QString defaultThemePath()
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return config + QStringLiteral("/qbar/themes/bliss-98-lock.css");
}

QString detectBackendName(const QString &requested)
{
    if (requested != QStringLiteral("auto")) {
        return requested;
    }
    if (!qgetenv("WAYLAND_DISPLAY").isEmpty()) {
        return QStringLiteral("wayland");
    }
    if (!qgetenv("DISPLAY").isEmpty()) {
        return QStringLiteral("x11");
    }
    return QStringLiteral("none");
}

std::unique_ptr<LockBackend> createBackend(const QString &name)
{
    if (name == QStringLiteral("x11")) {
        return std::make_unique<X11LockBackend>();
    }
    if (name == QStringLiteral("wayland")) {
        return std::make_unique<WaylandLockBackend>();
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName(QStringLiteral("qbar-lock"));
    QCoreApplication::setOrganizationName(QStringLiteral("qbar"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QBar QML/PAM lockscreen"));
    const QCommandLineOption helpOption = parser.addHelpOption();
    QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("Run without acquiring a real lock."));
    QCommandLineOption authOnStartOption(QStringLiteral("auth-on-start"),
                                         QStringLiteral("Start a PAM authentication attempt immediately, useful for pam_fprintd."));
    QCommandLineOption backendOption(QStringLiteral("backend"),
                                     QStringLiteral("Lock backend: auto, x11, wayland."),
                                     QStringLiteral("backend"),
                                     QStringLiteral("auto"));
    QCommandLineOption pamServiceOption(QStringLiteral("pam-service"),
                                        QStringLiteral("PAM service name."),
                                        QStringLiteral("service"),
                                        QStringLiteral("qbar-lock"));
    QCommandLineOption passwordPamServiceOption(QStringLiteral("password-pam-service"),
                                                QStringLiteral("PAM service for typed passwords; defaults to --pam-service."),
                                                QStringLiteral("service"));
    QCommandLineOption themeOption(QStringLiteral("theme"),
                                   QStringLiteral("CSS theme path."),
                                   QStringLiteral("path"),
                                   defaultThemePath());
    // NB: not "--style" — QGuiApplication reserves -style/--style for the widget style
    // and strips it from argv before QCommandLineParser sees it.
    QCommandLineOption styleOption(QStringLiteral("lock-style"),
                                   QStringLiteral("Lock face: panel (default) or ring (i3lock-style)."),
                                   QStringLiteral("style"),
                                   QStringLiteral("panel"));
    QCommandLineOption noFingerprintOption(QStringLiteral("no-fingerprint"),
                                           QStringLiteral("Disable fingerprint (fprintd) unlock."));
    QCommandLineOption noAvatarOption(QStringLiteral("no-avatar"),
                                      QStringLiteral("Hide the user's avatar photo (a monogram disc "
                                                     "is shown instead)."));
    QCommandLineOption facePamServiceOption(QStringLiteral("face-pam-service"),
                                            QStringLiteral("PAM service for face unlock (e.g. a pam_howdy stack); "
                                                           "unset disables face unlock."),
                                            QStringLiteral("service"));
    parser.addOption(demoOption);
    parser.addOption(authOnStartOption);
    parser.addOption(backendOption);
    parser.addOption(pamServiceOption);
    parser.addOption(passwordPamServiceOption);
    parser.addOption(themeOption);
    parser.addOption(styleOption);
    parser.addOption(noFingerprintOption);
    parser.addOption(noAvatarOption);
    parser.addOption(facePamServiceOption);

    // Parse argv BEFORE QGuiApplication. On Wayland the qbar-session-lock shell
    // integration acquires the REAL ext-session-lock as soon as the platform
    // plugin loads (i.e. inside the QGuiApplication constructor). Any invocation
    // that exits through the parser afterwards (--help, a mistyped flag) would
    // die holding the lock without unlock_and_destroy — and ext-session-lock-v1
    // is fail-secure, so the compositor keeps the session locked with no client
    // left to unlock it. Resolve every early-exit path here, pre-lock.
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    const bool parseOk = parser.parse(arguments);
    if (!parseOk || parser.isSet(helpOption) || parser.isSet(QStringLiteral("help-all"))) {
        // Exit path (help or usage error). A QCoreApplication loads no platform
        // plugin, so it cannot touch the compositor; it only lets the parser
        // print the real executable name instead of "<executable_name>".
        QCoreApplication helpApp(argc, argv);
        if (!parseOk) {
            fprintf(stderr, "%s\n", qPrintable(parser.errorText()));
            return 1;
        }
        parser.showHelp(0);
    }

    const bool demoMode = parser.isSet(demoOption);
    const bool authOnStart = parser.isSet(authOnStartOption);
    const QString backendName = detectBackendName(parser.value(backendOption));

    // Select the session-lock shell integration before the platform plugin loads —
    // but never for --demo: the integration locks the session for real at init,
    // and demo promises to run without acquiring a real lock.
    if (!demoMode && backendName == QStringLiteral("wayland")
        && qEnvironmentVariableIsEmpty("QT_WAYLAND_SHELL_INTEGRATION")) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "qbar-session-lock");
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("qbar-lock"));

    // LANG/LC_MESSAGES support: Qt's own catalogs (built-in component strings)
    // first, then ours (":/i18n", embedded via meson compile_translations).
    // Both live on main's stack, outliving app.exec().
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qt"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("qbar-lock"), QStringLiteral("_"),
                        QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&translator);
    }

    std::unique_ptr<LockBackend> backend = createBackend(backendName);

    PamAuthenticator authenticator;
    authenticator.setService(parser.value(pamServiceOption));

    CssTheme cssTheme;
    cssTheme.load(parser.value(themeOption));

    // Current user's avatar + real name (AccountsService via D-Bus, async) and the
    // Caps/Num Lock LED state — both shown on every lock face.
    UserModel userModel;
    CapsLockMonitor keyLocks;

    LockController controller(&authenticator, backend.get(), demoMode, parser.value(passwordPamServiceOption));
    controller.setFingerprintEnabled(!parser.isSet(noFingerprintOption));

    // Face unlock (howdy): a separate PAM authenticator on its own service, driven as a
    // continuous re-arming loop by the controller. Only enabled when a service is given.
    PamAuthenticator faceAuthenticator;
    const QString faceService = parser.value(facePamServiceOption).trimmed();
    if (!faceService.isEmpty()) {
        faceAuthenticator.setService(faceService);
        faceAuthenticator.setUser(authenticator.user());
        controller.setFaceAuthenticator(&faceAuthenticator);
    }

    // Lock face: the classic panel, or an i3lock-style single ring.
    const QUrl lockSource = parser.value(styleOption) == QStringLiteral("ring")
        ? QUrl(QStringLiteral("qrc:/lock/LockScreenRing.qml"))
        : QUrl(QStringLiteral("qrc:/lock/LockScreen.qml"));

    // The lock surface needs an alpha channel so translucent theme layers
    // (e.g. #lockscreen { overlay-color: rgba(...) } or a partly-transparent
    // background) composite through to what the compositor draws below, instead
    // of being painted over an opaque black clear color.
    QSurfaceFormat lockFormat = QSurfaceFormat::defaultFormat();
    lockFormat.setAlphaBufferSize(8);

    const bool onX11 = QGuiApplication::platformName() == QLatin1String("xcb");
    auto *x11Backend = qobject_cast<X11LockBackend *>(backend.get());

    // Outputs come and go while locked (dock/undock, a monitor powering off, all of
    // them vanishing at once). A lock face is kept per screen, created and destroyed
    // with it; the lock itself never depends on any window existing. Qt must not take
    // "no windows left" as "quit": a locker that exits on output loss leaves the
    // session locked with nothing able to unlock it.
    app.setQuitOnLastWindowClosed(false);

    QHash<QScreen *, QQuickView *> views;

    const auto syncX11Windows = [&]() {
        if (x11Backend == nullptr) {
            return;
        }
        QList<quintptr> lockWindowIds;
        lockWindowIds.reserve(views.size());
        for (const QQuickView *view : std::as_const(views)) {
            lockWindowIds.append(view->winId());
        }
        x11Backend->setLockWindows(lockWindowIds);
        // The grab must always sit on a live window (or the root): an X grab dies with
        // its window, so this runs BEFORE a lock window is destroyed.
        x11Backend->moveGrab(lockWindowIds.isEmpty() ? 0 : lockWindowIds.first());
    };

    const auto addView = [&](QScreen *screen) {
        if (screen == nullptr || views.contains(screen)) {
            return;
        }
        auto *view = new QQuickView;
        view->setFormat(lockFormat);
        view->setScreen(screen);
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->setColor(Qt::transparent);
        // On X11 the lock must be override-redirect: managed stacking (and the
        // StaysOnTop hint) never wins against override-redirect siblings such
        // as qbar's dock, which otherwise stays visible over the lock. The X11
        // backend then keeps the lock raised on every restack. Wayland is
        // untouched — ext-session-lock already guarantees the lock on top.
        Qt::WindowFlags lockFlags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
        if (onX11) {
            lockFlags |= Qt::BypassWindowManagerHint;
        }
        view->setFlags(lockFlags);
        view->setCursor(Qt::ArrowCursor);
        view->rootContext()->setContextProperty(QStringLiteral("lockController"), &controller);
        view->rootContext()->setContextProperty(QStringLiteral("cssTheme"), &cssTheme);
        view->rootContext()->setContextProperty(QStringLiteral("userModel"), &userModel);
        view->rootContext()->setContextProperty(QStringLiteral("lockHideAvatar"),
                                                parser.isSet(noAvatarOption));
        view->rootContext()->setContextProperty(QStringLiteral("keyLocks"), &keyLocks);
        view->setSource(lockSource);
        view->setGeometry(screen->geometry());
        if (onX11) {
            // Override-redirect: the geometry above already covers the screen;
            // showFullScreen would ask the absent WM for fullscreen state.
            view->show();
        } else {
            view->showFullScreen();
        }
        view->raise();
        view->requestActivate();
        views.insert(screen, view);
    };

    const auto removeView = [&](QScreen *screen) {
        QQuickView *view = views.take(screen);
        if (view == nullptr) {
            return;
        }
        syncX11Windows();
        view->deleteLater();
    };

    for (QScreen *screen : QGuiApplication::screens()) {
        addView(screen);
    }
    if (x11Backend != nullptr && !views.isEmpty()) {
        // Before lock(): the grab is taken on whichever window is set when it runs.
        x11Backend->setGrabWindow(views.begin().value()->winId());
        syncX11Windows();
    }

    const QMetaObject::Connection screenAddedConnection =
        QObject::connect(&app, &QGuiApplication::screenAdded, &app, [&](QScreen *screen) {
            addView(screen);
            syncX11Windows();
        });
    const QMetaObject::Connection screenRemovedConnection =
        QObject::connect(&app, &QGuiApplication::screenRemoved, &app, removeView);

    QObject::connect(&controller, &LockController::unlocked, &app, &QCoreApplication::quit);
    QTimer::singleShot(0, &controller, &LockController::start);
    if (authOnStart && !demoMode) {
        QTimer::singleShot(250, &controller, [&controller]() {
            controller.submitPassword(QString());
        });
    }

    const int result = app.exec();

    // Tear down in dependency order. The views reference controller/keyLocks/userModel
    // (main's locals, destroyed in reverse on return) through their QML contexts: delete
    // the views first, or their bindings evaluate against dead objects (the burst of
    // "Cannot read property ... of null"). The screen hooks capture these locals and
    // QGuiApplication's own teardown can still emit screenRemoved — cut them first.
    QObject::disconnect(screenAddedConnection);
    QObject::disconnect(screenRemovedConnection);
    qDeleteAll(views);
    views.clear();
    return result;
}
