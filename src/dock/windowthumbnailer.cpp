#include "windowthumbnailer.h"

#include <QDateTime>
#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>

#ifdef QBAR_HAVE_WAYLAND
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#endif

namespace {
constexpr qint64 kMinFrameIntervalMs = 250; // ≤ 4 fps while a preview is shown
constexpr int kThumbMaxSide = 480;          // stored size; the view scales down further
} // namespace

QImage WindowThumbProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // id = <key>/<serial>; the serial only busts QML's image cache.
    const QString key = id.section(QLatin1Char('/'), 0, 0);
    QImage image;
    {
        QMutexLocker locker(&m_cache->mutex);
        image = m_cache->images.value(key);
    }
    if (size != nullptr) {
        *size = image.size();
    }
    if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

#ifdef QBAR_HAVE_WAYLAND

struct WindowThumbnailer::Toplevel {
    WindowThumbnailer *self = nullptr;
    ext_foreign_toplevel_handle_v1 *handle = nullptr;
    QString title, appId, identifier;   // committed (after `done`)
    QString pTitle, pAppId, pIdentifier; // pending
};

struct WindowThumbnailer::Session {
    WindowThumbnailer *self = nullptr;
    QString key;
    ext_image_capture_source_v1 *source = nullptr;
    ext_image_copy_capture_session_v1 *session = nullptr;
    ext_image_copy_capture_frame_v1 *frame = nullptr; // in flight
    // Constraints advertised by the session (pending until `done`).
    uint32_t width = 0, height = 0, pWidth = 0, pHeight = 0;
    int32_t format = -1, pFormat = -1;
    bool constraintsKnown = false;
    // Our buffer.
    wl_buffer *buffer = nullptr;
    void *data = nullptr;
    size_t dataSize = 0;
    uint32_t bufWidth = 0, bufHeight = 0;
    int32_t bufFormat = -1;
    qint64 lastFrameMs = 0;
    bool wantFrame = false;
};

namespace {

using T = WindowThumbnailer;

QImage::Format qimageFormatFor(uint32_t shm)
{
    switch (shm) {
    case WL_SHM_FORMAT_ARGB8888: return QImage::Format_ARGB32_Premultiplied;
    case WL_SHM_FORMAT_XRGB8888: return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ABGR8888: return QImage::Format_RGBA8888_Premultiplied;
    case WL_SHM_FORMAT_XBGR8888: return QImage::Format_RGBX8888;
    default: return QImage::Format_Invalid;
    }
}

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *iface, uint32_t version)
{
    static_cast<T *>(data)->onGlobal(registry, name, iface, version);
}
void registryRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener kRegistryListener = {registryGlobal, registryRemove};

void listToplevel(void *data, ext_foreign_toplevel_list_v1 *, ext_foreign_toplevel_handle_v1 *handle)
{
    static_cast<T *>(data)->onToplevel(handle);
}
void listFinished(void *, ext_foreign_toplevel_list_v1 *) {}
const ext_foreign_toplevel_list_v1_listener kListListener = {listToplevel, listFinished};

void handleClosed(void *data, ext_foreign_toplevel_handle_v1 *)
{
    auto *t = static_cast<T::Toplevel *>(data);
    t->self->onToplevelClosed(t);
}
void handleDone(void *data, ext_foreign_toplevel_handle_v1 *)
{
    auto *t = static_cast<T::Toplevel *>(data);
    t->self->onToplevelDone(t);
}
void handleTitle(void *data, ext_foreign_toplevel_handle_v1 *, const char *title)
{
    static_cast<T::Toplevel *>(data)->pTitle = QString::fromUtf8(title);
}
void handleAppId(void *data, ext_foreign_toplevel_handle_v1 *, const char *appId)
{
    static_cast<T::Toplevel *>(data)->pAppId = QString::fromUtf8(appId);
}
void handleIdentifier(void *data, ext_foreign_toplevel_handle_v1 *, const char *id)
{
    static_cast<T::Toplevel *>(data)->pIdentifier = QString::fromUtf8(id);
}
const ext_foreign_toplevel_handle_v1_listener kHandleListener = {
    handleClosed, handleDone, handleTitle, handleAppId, handleIdentifier,
};

void sessionBufferSize(void *data, ext_image_copy_capture_session_v1 *, uint32_t w, uint32_t h)
{
    auto *s = static_cast<T::Session *>(data);
    s->pWidth = w;
    s->pHeight = h;
}
void sessionShmFormat(void *data, ext_image_copy_capture_session_v1 *, uint32_t format)
{
    qDebug("[dock] thumb session shm_format 0x%x", format);
    // Any 32-bit format QImage maps 1:1 on little-endian: ARGB8888/XRGB8888 (ARGB32 /
    // RGB32) or ABGR8888/XBGR8888 (RGBA8888 / RGBX8888). sway on a GLES2 renderer
    // offers ONLY XBGR8888, so accepting just the first pair never captured anything.
    auto *s = static_cast<T::Session *>(data);
    if (qimageFormatFor(format) != QImage::Format_Invalid && s->pFormat < 0) {
        s->pFormat = static_cast<int32_t>(format);
    }
}
void sessionDmabufDevice(void *, ext_image_copy_capture_session_v1 *, wl_array *) {}
void sessionDmabufFormat(void *, ext_image_copy_capture_session_v1 *, uint32_t, wl_array *) {}
void sessionDone(void *data, ext_image_copy_capture_session_v1 *)
{
    auto *s = static_cast<T::Session *>(data);
    s->self->onSessionDone(s);
}
void sessionStopped(void *data, ext_image_copy_capture_session_v1 *)
{
    auto *s = static_cast<T::Session *>(data);
    s->self->onSessionStopped(s);
}
const ext_image_copy_capture_session_v1_listener kSessionListener = {
    sessionBufferSize, sessionShmFormat, sessionDmabufDevice, sessionDmabufFormat, sessionDone, sessionStopped,
};

void frameTransform(void *, ext_image_copy_capture_frame_v1 *, uint32_t) {}
void frameDamage(void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t, int32_t, int32_t) {}
void framePresentation(void *, ext_image_copy_capture_frame_v1 *, uint32_t, uint32_t, uint32_t) {}
void frameReady(void *data, ext_image_copy_capture_frame_v1 *)
{
    auto *s = static_cast<T::Session *>(data);
    s->self->onFrameReady(s);
}
void frameFailed(void *data, ext_image_copy_capture_frame_v1 *, uint32_t reason)
{
    auto *s = static_cast<T::Session *>(data);
    s->self->onFrameFailed(s, reason);
}
const ext_image_copy_capture_frame_v1_listener kFrameListener = {
    frameTransform, frameDamage, framePresentation, frameReady, frameFailed,
};

} // namespace

WindowThumbnailer::WindowThumbnailer(QObject *parent)
    : QObject(parent)
{
    m_available = init();
    qInfo() << "[dock] window thumbnails:" << (m_available ? "ext-image-copy-capture" : "unavailable");
}

WindowThumbnailer::~WindowThumbnailer()
{
    const auto sessions = m_sessions.values();
    for (Session *s : sessions) {
        destroySession(s);
    }
    for (Toplevel *t : std::as_const(m_toplevels)) {
        ext_foreign_toplevel_handle_v1_destroy(t->handle);
        delete t;
    }
    m_toplevels.clear();
    if (m_list != nullptr) {
        ext_foreign_toplevel_list_v1_destroy(m_list);
    }
    if (m_sourceManager != nullptr) {
        ext_foreign_toplevel_image_capture_source_manager_v1_destroy(m_sourceManager);
    }
    if (m_copyManager != nullptr) {
        ext_image_copy_capture_manager_v1_destroy(m_copyManager);
    }
    if (m_shm != nullptr) {
        wl_shm_destroy(m_shm);
    }
    if (m_display != nullptr) {
        wl_display_flush(m_display);
    }
}

void WindowThumbnailer::onGlobal(wl_registry *registry, uint32_t name, const char *iface, uint32_t version)
{
    if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        m_shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
        m_list = static_cast<ext_foreign_toplevel_list_v1 *>(
            wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1));
        ext_foreign_toplevel_list_v1_add_listener(m_list, &kListListener, this);
    } else if (std::strcmp(iface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0) {
        m_sourceManager = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
    } else if (std::strcmp(iface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
        m_copyManager = static_cast<ext_image_copy_capture_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
    }
    Q_UNUSED(version);
}

bool WindowThumbnailer::init()
{
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        return false;
    }
    auto *native = QGuiApplication::platformNativeInterface();
    m_display = native != nullptr
        ? static_cast<wl_display *>(native->nativeResourceForIntegration(QByteArrayLiteral("display")))
        : nullptr;
    if (m_display == nullptr) {
        return false;
    }
    // Bind on a PRIVATE queue: a roundtrip on Qt's default queue would dispatch Qt's own
    // pending events (an output removal deleting a QScreen) from inside the dock's
    // construction. Two roundtrips: the binds, then the initial toplevels and their
    // title/app_id/identifier/done. Afterwards every proxy moves to the default queue
    // so later events arrive through Qt's normal dispatch, on the GUI thread.
    wl_event_queue *queue = wl_display_create_queue(m_display);
    if (queue == nullptr) {
        return false;
    }
    wl_registry *registry = wl_display_get_registry(m_display);
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(registry), queue);
    wl_registry_add_listener(registry, &kRegistryListener, this);
    wl_display_roundtrip_queue(m_display, queue);
    wl_display_roundtrip_queue(m_display, queue);
    wl_registry_destroy(registry);
    for (wl_proxy *p : {reinterpret_cast<wl_proxy *>(m_shm), reinterpret_cast<wl_proxy *>(m_list),
                        reinterpret_cast<wl_proxy *>(m_sourceManager),
                        reinterpret_cast<wl_proxy *>(m_copyManager)}) {
        if (p != nullptr) {
            wl_proxy_set_queue(p, nullptr);
        }
    }
    for (Toplevel *t : std::as_const(m_toplevels)) {
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(t->handle), nullptr);
    }
    wl_event_queue_destroy(queue);
    return m_shm != nullptr && m_list != nullptr && m_sourceManager != nullptr && m_copyManager != nullptr;
}

void WindowThumbnailer::onToplevel(ext_foreign_toplevel_handle_v1 *handle)
{
    auto *t = new Toplevel;
    t->self = this;
    t->handle = handle;
    ext_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener, t);
    m_toplevels.append(t);
}

void WindowThumbnailer::onToplevelDone(Toplevel *t)
{
    t->title = t->pTitle;
    t->appId = t->pAppId;
    if (!t->pIdentifier.isEmpty()) {
        t->identifier = t->pIdentifier;
    }
}

void WindowThumbnailer::onToplevelClosed(Toplevel *t)
{
    if (Session *s = m_sessions.value(t->identifier)) {
        destroySession(s);
    }
    m_toplevels.removeOne(t);
    ext_foreign_toplevel_handle_v1_destroy(t->handle);
    delete t;
}

QString WindowThumbnailer::request(const QString &appId, const QString &title)
{
    if (!m_available) {
        return {};
    }
    // Exact (app_id, title) first; then the title alone (XWayland app_id spellings
    // differ between the compositor IPC and foreign-toplevel); then a lone app_id match.
    Toplevel *best = nullptr;
    for (Toplevel *t : std::as_const(m_toplevels)) {
        if (t->title == title && t->appId.compare(appId, Qt::CaseInsensitive) == 0) {
            best = t;
            break;
        }
    }
    if (best == nullptr) {
        for (Toplevel *t : std::as_const(m_toplevels)) {
            if (!title.isEmpty() && t->title == title) {
                best = t;
                break;
            }
        }
    }
    if (best == nullptr) {
        Toplevel *only = nullptr;
        int count = 0;
        for (Toplevel *t : std::as_const(m_toplevels)) {
            if (t->appId.compare(appId, Qt::CaseInsensitive) == 0) {
                only = t;
                ++count;
            }
        }
        if (count == 1) {
            best = only;
        }
    }
    if (best == nullptr || best->identifier.isEmpty()) {
        return {};
    }
    Session *s = sessionFor(best);
    if (s == nullptr) {
        return {};
    }
    s->wantFrame = true;
    if (s->frame == nullptr && s->constraintsKnown
        && QDateTime::currentMSecsSinceEpoch() - s->lastFrameMs >= kMinFrameIntervalMs) {
        captureFrame(s);
    }
    return best->identifier;
}

void WindowThumbnailer::release(const QString &key)
{
    if (Session *s = m_sessions.value(key)) {
        destroySession(s);
    }
}

WindowThumbnailer::Session *WindowThumbnailer::sessionFor(Toplevel *t)
{
    if (Session *s = m_sessions.value(t->identifier)) {
        return s;
    }
    auto *s = new Session;
    s->self = this;
    s->key = t->identifier;
    s->source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(m_sourceManager, t->handle);
    s->session = ext_image_copy_capture_manager_v1_create_session(m_copyManager, s->source, 0);
    ext_image_copy_capture_session_v1_add_listener(s->session, &kSessionListener, s);
    m_sessions.insert(s->key, s);
    wl_display_flush(m_display);
    return s;
}

void WindowThumbnailer::destroySession(Session *s)
{
    m_sessions.remove(s->key);
    if (s->frame != nullptr) {
        ext_image_copy_capture_frame_v1_destroy(s->frame);
    }
    if (s->session != nullptr) {
        ext_image_copy_capture_session_v1_destroy(s->session);
    }
    if (s->source != nullptr) {
        ext_image_capture_source_v1_destroy(s->source);
    }
    freeBuffer(s);
    {
        QMutexLocker locker(&m_cache->mutex);
        m_cache->images.remove(s->key);
    }
    delete s;
    wl_display_flush(m_display);
}

void WindowThumbnailer::onSessionDone(Session *s)
{
    qDebug() << "[dock] thumb session done" << s->key << s->pWidth << "x" << s->pHeight << "format" << s->pFormat;
    s->width = s->pWidth;
    s->height = s->pHeight;
    s->format = s->pFormat;
    s->pFormat = -1;
    s->constraintsKnown = s->width > 0 && s->height > 0 && s->format >= 0;
    if (s->constraintsKnown && s->wantFrame && s->frame == nullptr) {
        captureFrame(s);
    }
}

void WindowThumbnailer::onSessionStopped(Session *s)
{
    destroySession(s); // the window went away or capture is no longer possible
}

bool WindowThumbnailer::ensureBuffer(Session *s)
{
    if (s->buffer != nullptr && s->bufWidth == s->width && s->bufHeight == s->height
        && s->bufFormat == s->format) {
        return true;
    }
    freeBuffer(s);
    const uint32_t stride = s->width * 4;
    const size_t size = static_cast<size_t>(stride) * s->height;
    const int fd = memfd_create("qbar-thumb", MFD_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        ::close(fd);
        return false;
    }
    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        ::close(fd);
        return false;
    }
    wl_shm_pool *pool = wl_shm_create_pool(m_shm, fd, static_cast<int32_t>(size));
    s->buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(s->width), static_cast<int32_t>(s->height),
                                          static_cast<int32_t>(stride), static_cast<uint32_t>(s->format));
    wl_shm_pool_destroy(pool);
    ::close(fd);
    s->data = data;
    s->dataSize = size;
    s->bufWidth = s->width;
    s->bufHeight = s->height;
    s->bufFormat = s->format;
    return s->buffer != nullptr;
}

void WindowThumbnailer::freeBuffer(Session *s)
{
    if (s->buffer != nullptr) {
        wl_buffer_destroy(s->buffer);
        s->buffer = nullptr;
    }
    if (s->data != nullptr) {
        munmap(s->data, s->dataSize);
        s->data = nullptr;
        s->dataSize = 0;
    }
}

void WindowThumbnailer::captureFrame(Session *s)
{
    if (!ensureBuffer(s)) {
        qDebug() << "[dock] thumb buffer allocation failed" << s->width << "x" << s->height;
        return;
    }
    qDebug() << "[dock] thumb capture" << s->key;
    s->wantFrame = false;
    s->frame = ext_image_copy_capture_session_v1_create_frame(s->session);
    ext_image_copy_capture_frame_v1_add_listener(s->frame, &kFrameListener, s);
    ext_image_copy_capture_frame_v1_attach_buffer(s->frame, s->buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(s->frame, 0, 0, static_cast<int32_t>(s->bufWidth),
                                                  static_cast<int32_t>(s->bufHeight));
    ext_image_copy_capture_frame_v1_capture(s->frame);
    wl_display_flush(m_display);
}

void WindowThumbnailer::onFrameReady(Session *s)
{
    ext_image_copy_capture_frame_v1_destroy(s->frame);
    s->frame = nullptr;
    s->lastFrameMs = QDateTime::currentMSecsSinceEpoch();
    const QImage::Format format = qimageFormatFor(static_cast<uint32_t>(s->bufFormat));
    const QImage view(static_cast<const uchar *>(s->data), static_cast<int>(s->bufWidth),
                      static_cast<int>(s->bufHeight), static_cast<qsizetype>(s->bufWidth) * 4, format);
    // Scale (and thereby copy out of the shared buffer) right away: the buffer is
    // reused for the next frame.
    QImage thumb = (static_cast<int>(s->bufWidth) > kThumbMaxSide || static_cast<int>(s->bufHeight) > kThumbMaxSide)
        ? view.scaled(kThumbMaxSide, kThumbMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : view.copy();
    {
        QMutexLocker locker(&m_cache->mutex);
        m_cache->images.insert(s->key, thumb);
    }
    const QString key = s->key;
    const int serial = ++m_serial;
    // Out of the wayland dispatch before QML reacts (bindings, image reloads).
    QMetaObject::invokeMethod(this, [this, key, serial]() { emit thumbnailUpdated(key, serial); },
                              Qt::QueuedConnection);
}

void WindowThumbnailer::onFrameFailed(Session *s, uint32_t reason)
{
    qDebug() << "[dock] thumb frame failed" << s->key << "reason" << reason;
    ext_image_copy_capture_frame_v1_destroy(s->frame);
    s->frame = nullptr;
    if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED) {
        destroySession(s);
        return;
    }
    // buffer_constraints: the window resized; the session sends new constraints + done,
    // and onSessionDone captures again with a fresh buffer.
    s->wantFrame = true;
}

#else // !QBAR_HAVE_WAYLAND

struct WindowThumbnailer::Toplevel {};
struct WindowThumbnailer::Session {};

WindowThumbnailer::WindowThumbnailer(QObject *parent) : QObject(parent) {}
WindowThumbnailer::~WindowThumbnailer() = default;
QString WindowThumbnailer::request(const QString &, const QString &) { return {}; }
void WindowThumbnailer::release(const QString &) {}
void WindowThumbnailer::onToplevel(ext_foreign_toplevel_handle_v1 *) {}
void WindowThumbnailer::onToplevelDone(Toplevel *) {}
void WindowThumbnailer::onToplevelClosed(Toplevel *) {}
void WindowThumbnailer::onSessionDone(Session *) {}
void WindowThumbnailer::onSessionStopped(Session *) {}
void WindowThumbnailer::onFrameReady(Session *) {}
void WindowThumbnailer::onFrameFailed(Session *, uint32_t) {}
void WindowThumbnailer::onGlobal(wl_registry *, uint32_t, const char *, uint32_t) {}
bool WindowThumbnailer::init() { return false; }
WindowThumbnailer::Session *WindowThumbnailer::sessionFor(Toplevel *) { return nullptr; }
void WindowThumbnailer::destroySession(Session *) {}
void WindowThumbnailer::captureFrame(Session *) {}
bool WindowThumbnailer::ensureBuffer(Session *) { return false; }
void WindowThumbnailer::freeBuffer(Session *) {}

#endif
