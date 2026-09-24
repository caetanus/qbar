#include "waylandgamma.h"

#ifdef QBAR_HAVE_WAYLAND

#include "wlr-gamma-control-unstable-v1-client-protocol.h"

#include <QByteArray>
#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace {

void registryGlobal(void *data, wl_registry *, uint32_t name, const char *interface, uint32_t version)
{
    static_cast<WaylandGammaBackend *>(data)->handleGlobal(name, interface, version);
}

void registryGlobalRemove(void *data, wl_registry *, uint32_t name)
{
    static_cast<WaylandGammaBackend *>(data)->handleGlobalRemove(name);
}

const wl_registry_listener registryListener = {
    registryGlobal,
    registryGlobalRemove,
};

void gammaSize(void *data, zwlr_gamma_control_v1 *, uint32_t size)
{
    auto *output = static_cast<WaylandGammaBackend::Output *>(data);
    output->self->handleGammaSize(output, size);
}

void gammaFailed(void *data, zwlr_gamma_control_v1 *)
{
    auto *output = static_cast<WaylandGammaBackend::Output *>(data);
    output->self->handleFailed(output);
}

const zwlr_gamma_control_v1_listener gammaListener = {
    gammaSize,
    gammaFailed,
};

bool writeAll(int fd, const void *data, size_t bytes)
{
    const auto *p = static_cast<const char *>(data);
    while (bytes > 0) {
        const ssize_t n = ::write(fd, p, bytes);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        p += n;
        bytes -= static_cast<size_t>(n);
    }
    return true;
}

} // namespace

WaylandGammaBackend::WaylandGammaBackend(QObject *parent)
    : GammaBackend(parent)
{
}

WaylandGammaBackend::~WaylandGammaBackend()
{
    for (auto &output : m_outputs) {
        destroyControl(output.get());
        if (output->output != nullptr) {
            wl_output_destroy(output->output);
        }
    }
    m_outputs.clear();
    if (m_manager != nullptr) {
        zwlr_gamma_control_manager_v1_destroy(m_manager);
    }
    if (m_registry != nullptr) {
        wl_registry_destroy(m_registry);
    }
    if (m_display != nullptr) {
        wl_display_flush(m_display);
    }
}

bool WaylandGammaBackend::init()
{
    auto *native = QGuiApplication::platformNativeInterface();
    m_display = native != nullptr
        ? static_cast<wl_display *>(native->nativeResourceForIntegration(QByteArrayLiteral("display")))
        : nullptr;
    if (m_display == nullptr) {
        return false;
    }
    m_registry = wl_display_get_registry(m_display);
    if (m_registry == nullptr) {
        return false;
    }
    wl_registry_add_listener(m_registry, &registryListener, this);
    // One roundtrip collects the initial globals (outputs + the manager); hotplug
    // events keep arriving later through Qt's dispatch of the default queue.
    wl_display_roundtrip(m_display);
    if (m_manager == nullptr) {
        qInfo() << "[redshift] compositor has no zwlr_gamma_control_manager_v1";
        return false;
    }
    return true;
}

QString WaylandGammaBackend::name() const
{
    return QStringLiteral("wlr-gamma-control");
}

bool WaylandGammaBackend::available() const
{
    if (m_manager == nullptr) {
        return false;
    }
    return std::any_of(m_outputs.begin(), m_outputs.end(), [](const auto &o) { return !o->failed; });
}

QString WaylandGammaBackend::status() const
{
    if (m_manager == nullptr) {
        return tr("no gamma-control protocol");
    }
    int ok = 0;
    int failed = 0;
    for (const auto &o : m_outputs) {
        (o->failed ? failed : ok)++;
    }
    if (m_outputs.empty()) {
        return tr("no outputs");
    }
    if (failed > 0 && ok == 0) {
        return tr("outputs owned by another gamma client");
    }
    return tr("%n output(s)", nullptr, ok);
}

void WaylandGammaBackend::handleGlobal(uint32_t name, const char *interface, uint32_t version)
{
    if (std::strcmp(interface, wl_output_interface.name) == 0) {
        auto output = std::make_unique<Output>();
        output->self = this;
        output->globalName = name;
        output->output = static_cast<wl_output *>(
            wl_registry_bind(m_registry, name, &wl_output_interface, std::min(version, 1U)));
        Output *raw = output.get();
        m_outputs.push_back(std::move(output));
        if (m_active) {
            ensureControl(raw);
        }
        notifyAvailability();
    } else if (std::strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        m_manager = static_cast<zwlr_gamma_control_manager_v1 *>(
            wl_registry_bind(m_registry, name, &zwlr_gamma_control_manager_v1_interface, 1));
        if (m_active) {
            for (auto &output : m_outputs) {
                ensureControl(output.get());
            }
        }
        notifyAvailability();
    }
}

void WaylandGammaBackend::handleGlobalRemove(uint32_t name)
{
    const auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
                                 [name](const auto &o) { return o->globalName == name; });
    if (it == m_outputs.end()) {
        return;
    }
    destroyControl(it->get());
    if ((*it)->output != nullptr) {
        wl_output_destroy((*it)->output);
    }
    m_outputs.erase(it);
    notifyAvailability();
}

void WaylandGammaBackend::ensureControl(Output *output)
{
    if (m_manager == nullptr || output->control != nullptr || output->failed) {
        return;
    }
    output->control = zwlr_gamma_control_manager_v1_get_gamma_control(m_manager, output->output);
    if (output->control != nullptr) {
        zwlr_gamma_control_v1_add_listener(output->control, &gammaListener, output);
    }
}

void WaylandGammaBackend::destroyControl(Output *output)
{
    if (output->control != nullptr) {
        zwlr_gamma_control_v1_destroy(output->control);
        output->control = nullptr;
    }
    output->rampSize = 0;
}

void WaylandGammaBackend::handleGammaSize(Output *output, uint32_t size)
{
    output->rampSize = size;
    sendRamps(output);
}

void WaylandGammaBackend::handleFailed(Output *output)
{
    qWarning() << "[redshift] gamma control refused for an output (owned by another client?)";
    destroyControl(output);
    output->failed = true;
    notifyAvailability();
}

void WaylandGammaBackend::sendRamps(Output *output)
{
    if (!m_active || output->control == nullptr || output->rampSize == 0) {
        return;
    }
    const size_t size = output->rampSize;
    std::vector<uint16_t> table(size * 3);
    fillGammaRamps(table.data(), size, m_whitepoint);

    const int fd = memfd_create("qbar-gamma", MFD_CLOEXEC);
    if (fd < 0) {
        qWarning() << "[redshift] memfd_create failed:" << std::strerror(errno);
        return;
    }
    if (!writeAll(fd, table.data(), table.size() * sizeof(uint16_t))) {
        qWarning() << "[redshift] writing the gamma table failed:" << std::strerror(errno);
        ::close(fd);
        return;
    }
    // libwayland dups the fd into the request, so ours can go right after.
    zwlr_gamma_control_v1_set_gamma(output->control, fd);
    wl_display_flush(m_display);
    ::close(fd);
}

void WaylandGammaBackend::apply(const colortemp::Whitepoint &wp)
{
    m_whitepoint = wp;
    m_active = true;
    for (auto &output : m_outputs) {
        ensureControl(output.get());
        sendRamps(output.get());  // no-op until gamma_size arrives, which then sends
    }
    if (m_display != nullptr) {
        wl_display_flush(m_display);
    }
}

void WaylandGammaBackend::release()
{
    m_active = false;
    for (auto &output : m_outputs) {
        destroyControl(output.get());  // the compositor restores its tables
        output->failed = false;        // a later apply() gets a fresh try
    }
    if (m_display != nullptr) {
        wl_display_flush(m_display);
    }
}

void WaylandGammaBackend::notifyAvailability()
{
    // Called from inside a wl_display dispatch; re-enter Qt on a clean stack.
    QMetaObject::invokeMethod(this, [this] { emit availabilityChanged(); }, Qt::QueuedConnection);
}

#endif // QBAR_HAVE_WAYLAND
