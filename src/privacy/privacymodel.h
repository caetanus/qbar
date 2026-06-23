#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

// waybar's "privacy" module: shows whether the microphone or camera is in use, and by which
// apps. Watches PipeWire for capture streams (media.class "Stream/Input/Audio" → mic,
// "Stream/Input/Video" → camera/screen) via the registry, integrated into the Qt event loop
// (a QSocketNotifier on the PipeWire loop fd — no extra thread). When built without
// libpipewire (QBAR_HAVE_PIPEWIRE undefined) it is an inert stub that reports nothing.
class PrivacyModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool micActive READ micActive NOTIFY changed)
    Q_PROPERTY(bool cameraActive READ cameraActive NOTIFY changed)
    Q_PROPERTY(QStringList micApps READ micApps NOTIFY changed)
    Q_PROPERTY(QStringList cameraApps READ cameraApps NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)

public:
    explicit PrivacyModel(QObject *parent = nullptr);
    ~PrivacyModel() override;

    bool micActive() const { return !m_micApps.isEmpty(); }
    bool cameraActive() const { return !m_cameraApps.isEmpty(); }
    bool active() const { return micActive() || cameraActive(); }
    QStringList micApps() const { return m_micApps; }
    QStringList cameraApps() const { return m_cameraApps; }
    QString tooltipText() const;

    // Called by the PipeWire backend when the active capture set changes.
    void applyState(const QStringList &mic, const QStringList &camera);

    // PipeWire state, defined in the .cpp. Public so the registry C callbacks can name it;
    // the m_impl member itself stays private.
    struct Impl;

signals:
    void changed();

private:
    QStringList m_micApps;
    QStringList m_cameraApps;
    std::unique_ptr<Impl> m_impl;
};
