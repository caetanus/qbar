#include "modelcapsules.h"

#include <QCoreApplication>
#include <QQmlEngine>

ModelCapsules *ModelCapsules::instance()
{
    static ModelCapsules *self = new ModelCapsules(qApp);
    return self;
}

ModelCapsules::ModelCapsules(QObject *parent)
    : QObject(parent)
{
}

QObject *ModelCapsules::acquire(const QString &key, QWindow *window)
{
    QObject *model = nullptr;
    if (key == QLatin1String("cpu")) {
        model = m_cpu.get();
    } else if (key == QLatin1String("temperature")) {
        model = m_temperature.get();
    } else if (key == QLatin1String("network")) {
        model = m_network.get();
    } else if (key == QLatin1String("networkProcess")) {
        model = m_networkProcess.get();
    } else if (key == QLatin1String("networkManager")) {
        model = m_networkManager.get();
    } else if (key == QLatin1String("brightness")) {
        model = m_brightness.get();
    } else if (key == QLatin1String("mpris")) {
        model = m_mpris.get();
    } else if (key == QLatin1String("calendar")) {
        model = m_calendar.get();
    } else if (key == QLatin1String("battery")) {
        model = m_battery.get();
    } else if (key == QLatin1String("tray")) {
        model = m_tray.get();
    } else if (key == QLatin1String("sound")) {
        model = m_sound.get();
    } else if (key == QLatin1String("caffeine")) {
        if (m_caffeineWindow == nullptr) {
            m_caffeineWindow = window;
        }
        model = m_caffeine.get();
    } else if (key == QLatin1String("disk")) {
        model = m_disk.get();
    } else if (key == QLatin1String("bluetooth")) {
        model = m_bluetooth.get();
    } else if (key == QLatin1String("powerProfiles")) {
        model = m_powerProfiles.get();
    } else if (key == QLatin1String("upower")) {
        model = m_upower.get();
    } else if (key == QLatin1String("user")) {
        model = m_user.get();
    } else if (key == QLatin1String("privacy")) {
        model = m_privacy.get();
    } else {
        qWarning("ModelCapsules: unknown model '%s'", qPrintable(key));
        return nullptr;
    }
    // The model is owned by C++ (parented to this process-wide singleton); make sure a QML
    // context that receives it never garbage-collects it.
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}
