#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>

#include <cstdint>
#include <memory>

struct wl_registry;
struct ext_foreign_toplevel_handle_v1;

// Live window thumbnails for the dock's hover preview.
//
// Wayland (wlroots ≥ 0.19 / sway ≥ 1.11): ext-foreign-toplevel-list-v1 enumerates the
// toplevels (title, app_id, stable identifier); ext-foreign-toplevel-image-capture-source-v1
// turns one into a capture source; ext-image-copy-capture-v1 copies its pixels into a
// wl_shm buffer we own. The copy covers the whole window even when it is covered or on
// another workspace. Captures run only on demand (request() while the preview is shown),
// one frame in flight, so an idle dock costs nothing.
//
// Elsewhere (X11, compositors without the protocols) available() is false and the dock
// keeps showing just the icon.
//
// QML: `windowThumbs.request(appId, title)` → a key ("" = no match / unavailable); the
// image is `image://windowthumb/<key>/<serial>`, `thumbnailUpdated(key, serial)` bumps the
// serial whenever a new frame lands. `release(key)` ends that capture session.

struct ThumbCache {
    QMutex mutex;
    QHash<QString, QImage> images;
};

class WindowThumbProvider final : public QQuickImageProvider {
public:
    explicit WindowThumbProvider(std::shared_ptr<ThumbCache> cache)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_cache(std::move(cache))
    {
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    std::shared_ptr<ThumbCache> m_cache; // outlives the thumbnailer if the engine does
};

class WindowThumbnailer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit WindowThumbnailer(QObject *parent = nullptr);
    ~WindowThumbnailer() override;

    bool available() const { return m_available; }
    std::shared_ptr<ThumbCache> cache() const { return m_cache; }

    // Find the toplevel for (appId, title) and keep its capture live; returns its key or "".
    // Call it periodically (the preview's timer) — each call may take one new frame, at
    // most every kMinFrameIntervalMs.
    Q_INVOKABLE QString request(const QString &appId, const QString &title);
    Q_INVOKABLE void release(const QString &key);

    // Wayland listener entry points (public for the C callbacks).
    struct Toplevel;
    struct Session;
    void onToplevel(struct ext_foreign_toplevel_handle_v1 *handle);
    void onToplevelDone(Toplevel *t);
    void onToplevelClosed(Toplevel *t);
    void onSessionDone(Session *s);
    void onSessionStopped(Session *s);
    void onFrameReady(Session *s);
    void onFrameFailed(Session *s, uint32_t reason);
    void onGlobal(struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);

signals:
    void thumbnailUpdated(const QString &key, int serial);

private:
    bool init();
    Session *sessionFor(Toplevel *t);
    void destroySession(Session *s);
    void captureFrame(Session *s);
    bool ensureBuffer(Session *s);
    void freeBuffer(Session *s);

    std::shared_ptr<ThumbCache> m_cache = std::make_shared<ThumbCache>();
    bool m_available = false;
    struct wl_display *m_display = nullptr;
    struct wl_shm *m_shm = nullptr;
    struct ext_foreign_toplevel_list_v1 *m_list = nullptr;
    struct ext_foreign_toplevel_image_capture_source_manager_v1 *m_sourceManager = nullptr;
    struct ext_image_copy_capture_manager_v1 *m_copyManager = nullptr;
    QList<Toplevel *> m_toplevels;
    QHash<QString, Session *> m_sessions; // by toplevel identifier
    int m_serial = 0;
};
