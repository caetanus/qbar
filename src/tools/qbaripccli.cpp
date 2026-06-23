// qbar-ipc — a tiny client for qbar's JSON IPC (see src/qml/qbaripc.{h,cpp}).
//
// Meant to be bound to a key in your compositor, e.g. (sway/i3):
//   bindsym $mod+c exec qbar-ipc toggle cpu
//   bindsym $mod+m exec qbar-ipc toggle memory
//
// Usage:
//   qbar-ipc toggle|open|close <popup-or-drawer>
//   qbar-ipc trigger <command>        (run a custom action a widget registered)
//   qbar-ipc close-all | list | commands | ping
//   qbar-ipc --socket <name> ...      (default: qbar, or $QBAR_IPC_SOCKET)
//
// The name may be a popup OR a drawer group (a `group/<name>` with a `drawer`); opening a
// drawer pins it expanded, e.g.  bindsym $mod+t exec qbar-ipc toggle tools
//
// A custom widget can register an action (via qbarIpc.registerCommand) — e.g. the speed-test
// widget exposes "speedtest-run", so:  bindsym $mod+s exec qbar-ipc trigger speedtest-run
//
// Prints the JSON reply; exit status is 0 when the reply is {"ok":true}, else non-zero.

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>

#include <cstdio>

namespace {

void usage()
{
    std::fputs(
        "usage: qbar-ipc <command> [arg]\n"
        "  toggle <popup>   open <popup>   close <popup>\n"
        "  trigger <command>   (run a widget-registered action)\n"
        "  set-css <path|url>   (preview a theme; relative paths resolve from the cwd)\n"
        "  reset-css   (revert set-css back to the configured theme)\n"
        "  close-all   list   commands   ping\n"
        "  --socket <path>  (default: $XDG_RUNTIME_DIR/qbar.sock, or $QBAR_IPC_SOCKET)\n",
        stderr);
}

QString defaultSocket()
{
    const QString env = qEnvironmentVariable("QBAR_IPC_SOCKET");
    if (!env.isEmpty()) {
        return env;
    }
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) {
        runtime = QDir::tempPath();
    }
    return runtime + QStringLiteral("/qbar.sock");
}

QByteArray sendRequest(const QString &socketName, const QJsonObject &request)
{
    QLocalSocket sock;
    sock.connectToServer(socketName);
    if (!sock.waitForConnected(1000)) {
        return {};
    }
    sock.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    sock.waitForBytesWritten(1000);
    if (sock.waitForReadyRead(1000)) {
        return sock.readLine().trimmed();
    }
    return {};
}

// Query the running qbar and print the names you can toggle/open/close.
void printToggles(const QString &socketName)
{
    const QByteArray resp = sendRequest(socketName, QJsonObject{{QStringLiteral("command"),
                                                                 QStringLiteral("list")}});
    const QJsonArray names = QJsonDocument::fromJson(resp).object()
                                 .value(QStringLiteral("popups")).toArray();
    if (names.isEmpty()) {
        std::fputs("\nAvailable toggles: (qbar not running, or none registered)\n", stderr);
        return;
    }
    QStringList list;
    for (const QJsonValue &v : names) {
        list.append(v.toString());
    }
    list.sort();
    std::fprintf(stderr, "\nAvailable toggles: %s\n", qPrintable(list.join(QStringLiteral(", "))));
}

// Query the running qbar and print the custom actions you can `trigger`.
void printCommands(const QString &socketName)
{
    const QByteArray resp = sendRequest(socketName, QJsonObject{{QStringLiteral("command"),
                                                                 QStringLiteral("commands")}});
    const QJsonArray names = QJsonDocument::fromJson(resp).object()
                                 .value(QStringLiteral("commands")).toArray();
    if (names.isEmpty()) {
        return; // nothing registered (or qbar not running) — stay quiet, printToggles covers that
    }
    QStringList list;
    for (const QJsonValue &v : names) {
        list.append(v.toString());
    }
    list.sort();
    std::fprintf(stderr, "Triggerable commands: %s\n", qPrintable(list.join(QStringLiteral(", "))));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QStringList args = QCoreApplication::arguments().mid(1);
    QString socketName = defaultSocket();

    for (int i = 0; i < args.size();) {
        if (args.at(i) == QLatin1String("--socket") && i + 1 < args.size()) {
            socketName = args.at(i + 1);
            args.removeAt(i);
            args.removeAt(i);
        } else if (args.at(i) == QLatin1String("-h") || args.at(i) == QLatin1String("--help")) {
            usage();
            return 0;
        } else {
            ++i;
        }
    }

    if (args.isEmpty()) {
        usage();
        printToggles(socketName);  // show what's actually registered right now
        printCommands(socketName);
        return 2;
    }

    const QString command = args.takeFirst();
    QJsonObject request;
    request.insert(QStringLiteral("command"), command);

    if (command == QLatin1String("toggle") || command == QLatin1String("open")
        || command == QLatin1String("close")) {
        if (args.isEmpty()) {
            std::fprintf(stderr, "qbar-ipc: '%s' needs a popup name\n", qPrintable(command));
            return 2;
        }
        request.insert(QStringLiteral("popup"), args.takeFirst());
    } else if (command == QLatin1String("trigger")) {
        if (args.isEmpty()) {
            std::fputs("qbar-ipc: 'trigger' needs a command name\n", stderr);
            return 2;
        }
        request.insert(QStringLiteral("name"), args.takeFirst());
    } else if (command == QLatin1String("set-css")) {
        if (args.isEmpty()) {
            std::fputs("qbar-ipc: 'set-css' needs a path or URL\n", stderr);
            return 2;
        }
        QString arg = args.takeFirst();
        // The qbar daemon's working directory differs from ours, so a relative path means
        // nothing to it. Resolve a relative path that exists here (the dir the user ran
        // qbar-ipc from) to an absolute one before sending. URLs and absolute paths pass through.
        if (!arg.contains(QStringLiteral("://"))) {
            const QFileInfo fi(arg);
            if (fi.isRelative() && fi.exists()) {
                arg = fi.absoluteFilePath();
            }
        }
        request.insert(QStringLiteral("path"), arg);
    }

    QLocalSocket sock;
    sock.connectToServer(socketName);
    if (!sock.waitForConnected(1000)) {
        std::fprintf(stderr, "qbar-ipc: cannot connect to '%s': %s\n",
                     qPrintable(socketName), qPrintable(sock.errorString()));
        return 1;
    }

    sock.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    sock.waitForBytesWritten(1000);

    QByteArray response;
    if (sock.waitForReadyRead(1000)) {
        response = sock.readLine().trimmed();
    }
    if (!response.isEmpty()) {
        std::fputs(response.constData(), stdout);
        std::fputc('\n', stdout);
    }

    return QJsonDocument::fromJson(response).object().value(QStringLiteral("ok")).toBool() ? 0 : 1;
}
