#include "brightnessmodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <algorithm>

namespace {

constexpr auto kBacklightRoot = "/sys/class/backlight";

} // namespace

BrightnessModel::BrightnessModel(QObject *parent)
    : QObject(parent)
{
    refresh();
    QTimer::singleShot(1000, this, &BrightnessModel::refresh);
    m_timer.setInterval(1000);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &BrightnessModel::refresh);
    m_timer.start();
}

bool BrightnessModel::available() const
{
    return m_available;
}

int BrightnessModel::brightness() const
{
    return m_brightness;
}

int BrightnessModel::maxBrightness() const
{
    return m_maxBrightness;
}

int BrightnessModel::percent() const
{
    if (m_brightness < 0 || m_maxBrightness <= 0) {
        return 0;
    }

    return qBound(0, (m_brightness * 100) / m_maxBrightness, 100);
}

QString BrightnessModel::deviceName() const
{
    return m_deviceName;
}

QString BrightnessModel::tooltipText() const
{
    if (!m_available) {
        return QStringLiteral("brightness unavailable");
    }

    const QString name = m_deviceName.isEmpty() ? QStringLiteral("brightness") : m_deviceName;
    return QStringLiteral("%1 | %2%").arg(name, QString::number(percent()));
}

void BrightnessModel::stepUp(int percent)
{
    adjustPercent(std::max(1, percent));
}

void BrightnessModel::stepDown(int percent)
{
    adjustPercent(-std::max(1, percent));
}

void BrightnessModel::setPercent(int percentValue)
{
    if (!m_available || m_maxBrightness <= 0) {
        return;
    }

    const int clamped = qBound(0, percentValue, 100);
    const int target = qRound((m_maxBrightness * clamped) / 100.0);
    if (writeIntegerFile(m_devicePath + QStringLiteral("/brightness"), target)) {
        refresh();
    }
}

int BrightnessModel::readIntegerFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }

    const QString text = QString::fromUtf8(file.readAll()).trimmed();
    bool ok = false;
    const int value = text.toInt(&ok);
    return ok ? value : -1;
}

bool BrightnessModel::writeIntegerFile(const QString &path, int value)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[brightness] failed to open for write:" << path;
        return false;
    }

    QTextStream stream(&file);
    stream << value << '\n';
    if (stream.status() != QTextStream::Ok) {
        qWarning() << "[brightness] failed to write value:" << path << value;
        return false;
    }
    return true;
}

QString BrightnessModel::selectBrightnessDevice()
{
    QDir root(QString::fromLatin1(kBacklightRoot));
    const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QString bestPath;
    int bestMax = -1;

    for (const QFileInfo &entry : entries) {
        const QString path = entry.absoluteFilePath();
        const int maxBrightness = readIntegerFile(path + QStringLiteral("/max_brightness"));
        const int currentBrightness = readIntegerFile(path + QStringLiteral("/brightness"));
        if (maxBrightness <= 0 || currentBrightness < 0) {
            continue;
        }
        if (maxBrightness > bestMax) {
            bestMax = maxBrightness;
            bestPath = path;
        }
    }

    return bestPath;
}

BrightnessModel::DeviceState BrightnessModel::readDeviceState(const QString &path) const
{
    DeviceState state;
    if (path.isEmpty()) {
        return state;
    }

    const QFileInfo info(path);
    const int maxBrightness = readIntegerFile(path + QStringLiteral("/max_brightness"));
    const int actualBrightness = readIntegerFile(path + QStringLiteral("/actual_brightness"));
    const int brightness = actualBrightness >= 0 ? actualBrightness : readIntegerFile(path + QStringLiteral("/brightness"));

    state.path = path;
    state.name = info.fileName();
    state.maxBrightness = maxBrightness;
    state.brightness = brightness;
    state.writable = QFileInfo(path + QStringLiteral("/brightness")).isWritable();
    state.valid = maxBrightness > 0 && brightness >= 0;
    return state;
}

void BrightnessModel::refresh()
{
    const QString selectedPath = selectBrightnessDevice();
    const DeviceState state = readDeviceState(selectedPath);
    setState(state);
}

void BrightnessModel::setState(const DeviceState &state)
{
    const bool availableChanged = m_available != state.valid;
    const bool dataChanged = m_devicePath != state.path
        || m_deviceName != state.name
        || m_brightness != state.brightness
        || m_maxBrightness != state.maxBrightness
        || m_writable != state.writable
        || m_available != state.valid;

    m_devicePath = state.path;
    m_deviceName = state.name;
    m_brightness = state.brightness;
    m_maxBrightness = state.maxBrightness;
    m_writable = state.writable;
    m_available = state.valid;

    if (availableChanged) {
        emit availabilityChanged();
    }
    if (dataChanged) {
        emit brightnessChanged();
    }
}

void BrightnessModel::adjustPercent(int deltaPercent)
{
    if (!m_available || m_maxBrightness <= 0) {
        return;
    }

    const int targetPercent = qBound(0, percent() + deltaPercent, 100);
    setPercent(targetPercent);
}

