#pragma once

#include "audiobackend.h"

// WirePlumber's headers (and the PipeWire/SPA headers they pull in) define
// macros that clash with moc's processing of Q_OBJECT, so they must NOT be
// included here. Forward-declare the GObject types as opaque and keep all glib
// types out of this header — <wp/wp.h> is included only in the .cpp.
typedef struct _WpCore WpCore;
typedef struct _WpObjectManager WpObjectManager;
typedef struct _WpPlugin WpPlugin;

// Native WirePlumber (PipeWire) audio backend. Uses the WirePlumber client
// library's mixer-api / default-nodes-api plugins to track the default sink and
// source and to get/set volume + mute. The WpCore runs on the process default
// GMainContext, which Qt iterates through its GLib event dispatcher (default on
// Linux), so all callbacks land on the GUI thread — no extra thread needed.
// Selected at build time instead of SoundModel when wireplumber-0.5 is present
// (see audiobackendfactory.h).
class WirePlumberBackend final : public AudioBackend {
    Q_OBJECT

public:
    explicit WirePlumberBackend(QObject *parent = nullptr);
    ~WirePlumberBackend() override;

    void stepUp(int percent = 5) override;
    void stepDown(int percent = 5) override;
    void setPercent(int percent) override;
    void toggleMute() override;
    void stepSourceUp(int percent = 5) override;
    void stepSourceDown(int percent = 5) override;
    void setSourcePercent(int percent) override;
    void toggleSourceMute() override;

private:
    // WirePlumber/GObject callbacks (run on the GUI thread via the GLib loop).
    // glib parameter types are erased to void*/unsigned to keep this header
    // moc-safe; the .cpp casts them back to their real types.
    static void onPluginLoaded(void *object, void *result, void *data);
    static void onObjectManagerInstalled(void *om, void *data);
    static void onDefaultNodesChanged(void *api, void *data);
    static void onMixerChanged(void *api, unsigned id, void *data);

    void maybeActivate();         // both plugins loaded → install object manager
    void refresh();               // re-read sink + source state
    State readNode(const char *mediaClass);
    void setNodeVolume(const char *mediaClass, int percent);
    void toggleNodeMute(const char *mediaClass, bool currentlyMuted);
    unsigned defaultNodeId(const char *mediaClass);

    WpCore *m_core = nullptr;
    WpObjectManager *m_om = nullptr;
    WpPlugin *m_mixer = nullptr;
    WpPlugin *m_defaultNodes = nullptr;
    int m_pendingPlugins = 0;
    bool m_ready = false;
};
