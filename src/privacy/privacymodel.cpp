#include "privacymodel.h"

#if QBAR_HAVE_PIPEWIRE
#include <QHash>
#include <QPair>
#include <QSocketNotifier>

#include <pipewire/pipewire.h>

#include <cstring>
#endif

// Impl is always defined (so std::unique_ptr<Impl> has a complete type regardless of the
// build), but only carries PipeWire state when compiled with libpipewire.
struct PrivacyModel::Impl {
#if QBAR_HAVE_PIPEWIRE
    PrivacyModel *q = nullptr;
    pw_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    spa_hook registryListener {};
    QSocketNotifier *notifier = nullptr;
    // node id → { category (0 = mic, 1 = camera/video), app label }
    QHash<uint32_t, QPair<int, QString>> nodes;

    void recompute()
    {
        QStringList mic;
        QStringList cam;
        for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
            QStringList &target = it.value().first == 0 ? mic : cam;
            const QString &app = it.value().second;
            if (!target.contains(app)) {
                target.append(app);
            }
        }
        q->applyState(mic, cam);
    }
#endif
};

#if QBAR_HAVE_PIPEWIRE
namespace {

void onGlobal(void *data, uint32_t id, uint32_t /*permissions*/, const char *type,
              uint32_t /*version*/, const struct spa_dict *props)
{
    if (props == nullptr || type == nullptr || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) {
        return;
    }
    const char *mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (mediaClass == nullptr) {
        return;
    }
    int category = -1;
    if (std::strcmp(mediaClass, "Stream/Input/Audio") == 0) {
        category = 0; // microphone capture
    } else if (std::strcmp(mediaClass, "Stream/Input/Video") == 0) {
        category = 1; // camera / screen capture
    } else {
        return;
    }
    const char *app = spa_dict_lookup(props, PW_KEY_APP_NAME);
    if (app == nullptr) {
        app = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    }
    if (app == nullptr) {
        app = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    }
    auto *impl = static_cast<PrivacyModel::Impl *>(data);
    impl->nodes.insert(id, qMakePair(category, app != nullptr ? QString::fromUtf8(app)
                                                              : QStringLiteral("Unknown")));
    impl->recompute();
}

void onGlobalRemove(void *data, uint32_t id)
{
    auto *impl = static_cast<PrivacyModel::Impl *>(data);
    if (impl->nodes.remove(id) > 0) {
        impl->recompute();
    }
}

const struct pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = onGlobal,
    .global_remove = onGlobalRemove,
};

} // namespace
#endif // QBAR_HAVE_PIPEWIRE

PrivacyModel::PrivacyModel(QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
#if QBAR_HAVE_PIPEWIRE
    m_impl->q = this;
    pw_init(nullptr, nullptr);
    m_impl->loop = pw_loop_new(nullptr);
    if (m_impl->loop == nullptr) {
        return;
    }
    pw_loop_enter(m_impl->loop);
    m_impl->context = pw_context_new(m_impl->loop, nullptr, 0);
    if (m_impl->context == nullptr) {
        return;
    }
    m_impl->core = pw_context_connect(m_impl->context, nullptr, 0);
    if (m_impl->core == nullptr) {
        return; // PipeWire not running — stay inert
    }
    m_impl->registry = pw_core_get_registry(m_impl->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(m_impl->registry, &m_impl->registryListener, &kRegistryEvents, m_impl.get());

    // Drive the PipeWire loop from the Qt event loop: its fd becomes readable when there's
    // work, and pw_loop_iterate(…, 0) processes it (registry callbacks run here, GUI thread).
    m_impl->notifier = new QSocketNotifier(pw_loop_get_fd(m_impl->loop), QSocketNotifier::Read, this);
    connect(m_impl->notifier, &QSocketNotifier::activated, this, [this]() {
        if (m_impl->loop != nullptr) {
            pw_loop_iterate(m_impl->loop, 0);
        }
    });
#endif
}

PrivacyModel::~PrivacyModel()
{
#if QBAR_HAVE_PIPEWIRE
    if (m_impl->loop != nullptr) {
        if (m_impl->registry != nullptr) {
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_impl->registry));
        }
        if (m_impl->core != nullptr) {
            pw_core_disconnect(m_impl->core);
        }
        if (m_impl->context != nullptr) {
            pw_context_destroy(m_impl->context);
        }
        pw_loop_leave(m_impl->loop);
        pw_loop_destroy(m_impl->loop);
    }
#endif
}

void PrivacyModel::applyState(const QStringList &mic, const QStringList &camera)
{
    if (mic == m_micApps && camera == m_cameraApps) {
        return;
    }
    m_micApps = mic;
    m_cameraApps = camera;
    emit changed();
}

QString PrivacyModel::tooltipText() const
{
    if (!active()) {
        return QStringLiteral("Microphone and camera idle");
    }
    QStringList lines;
    if (micActive()) {
        lines.append(QStringLiteral("🎤 Microphone: %1").arg(m_micApps.join(QStringLiteral(", "))));
    }
    if (cameraActive()) {
        lines.append(QStringLiteral("📹 Camera: %1").arg(m_cameraApps.join(QStringLiteral(", "))));
    }
    return lines.join(QLatin1Char('\n'));
}
