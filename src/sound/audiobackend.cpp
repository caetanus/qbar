#include "audiobackend.h"

#include <QStringList>

void AudioBackend::applySinkState(const State &state)
{
    const bool availabilityFlip = m_available != state.available;
    const bool dataChanged = m_sinkName != state.name
        || m_sinkDescription != state.description
        || m_sinkFormFactor != state.formFactor
        || m_volume != state.volume
        || m_muted != state.muted
        || m_available != state.available;

    m_sinkName = state.name;
    m_sinkDescription = state.description;
    m_sinkFormFactor = state.formFactor;
    m_volume = state.volume;
    m_muted = state.muted;
    m_available = state.available;
    m_sinkChannels = state.channels;

    if (availabilityFlip) {
        emit availabilityChanged();
    }
    if (dataChanged) {
        emit volumeChanged();
    }
}

void AudioBackend::applySourceState(const State &state)
{
    const bool availabilityFlip = m_sourceAvailable != state.available;
    const bool dataChanged = m_sourceName != state.name
        || m_sourceDescription != state.description
        || m_sourceFormFactor != state.formFactor
        || m_sourceVolume != state.volume
        || m_sourceMuted != state.muted
        || m_sourceAvailable != state.available;

    m_sourceName = state.name;
    m_sourceDescription = state.description;
    m_sourceFormFactor = state.formFactor;
    m_sourceVolume = state.volume;
    m_sourceMuted = state.muted;
    m_sourceAvailable = state.available;
    m_sourceChannels = state.channels;

    if (availabilityFlip) {
        emit availabilityChanged();
    }
    if (dataChanged) {
        emit volumeChanged();
    }
}

QString AudioBackend::friendlySinkName() const
{
    if (m_sinkName.isEmpty()) {
        return QStringLiteral("default sink");
    }
    return m_sinkDescription.isEmpty() ? m_sinkName : m_sinkDescription;
}

QString AudioBackend::friendlySourceName() const
{
    if (m_sourceName.isEmpty()) {
        return QStringLiteral("default source");
    }
    return m_sourceDescription.isEmpty() ? m_sourceName : m_sourceDescription;
}

QString AudioBackend::formFactorIconName(const QString &formFactor)
{
    if (formFactor == QStringLiteral("headset") || formFactor == QStringLiteral("hands-free")) {
        return QStringLiteral("audio-headset-symbolic");
    }
    if (formFactor == QStringLiteral("headphone") || formFactor == QStringLiteral("portable")) {
        return QStringLiteral("audio-headphones-symbolic");
    }
    return {};
}

QString AudioBackend::displayText() const
{
    if (!m_available || m_volume < 0) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1%").arg(m_volume);
}

QString AudioBackend::tooltipText() const
{
    QStringList parts;
    if (m_available) {
        parts << friendlySinkName();
        if (m_muted) {
            parts << QStringLiteral("muted");
        }
        parts << QStringLiteral("%1%").arg(m_volume);
    } else {
        parts << QStringLiteral("sound unavailable");
    }
    return parts.join(QStringLiteral(" | "));
}

QString AudioBackend::outputTooltipText() const
{
    if (!m_available) {
        return QStringLiteral("sound unavailable");
    }
    const QStringList parts = m_muted
        ? QStringList{friendlySinkName(), QStringLiteral("muted"), QStringLiteral("%1%").arg(m_volume)}
        : QStringList{friendlySinkName(), QStringLiteral("%1%").arg(m_volume)};
    return parts.join(QStringLiteral(" | "));
}

QString AudioBackend::sourceDisplayText() const
{
    if (!m_sourceAvailable || m_sourceVolume < 0) {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1%").arg(m_sourceVolume);
}

QString AudioBackend::sourceTooltipText() const
{
    if (!m_sourceAvailable) {
        return QStringLiteral("mic unavailable");
    }
    const QStringList parts = m_sourceMuted
        ? QStringList{friendlySourceName(), QStringLiteral("muted"), QStringLiteral("%1%").arg(m_sourceVolume)}
        : QStringList{friendlySourceName(), QStringLiteral("%1%").arg(m_sourceVolume)};
    return parts.join(QStringLiteral(" | "));
}

QString AudioBackend::outputIconName() const
{
    return formFactorIconName(m_sinkFormFactor);
}

QString AudioBackend::inputIconName() const
{
    return formFactorIconName(m_sourceFormFactor);
}
