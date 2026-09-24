#pragma once

#ifdef QBAR_HAVE_XCB_RANDR

#include "gammabackend.h"

struct xcb_connection_t;

// XRandR CRTC gamma (Xorg). Own connection, like the Caffeine screensaver suspend;
// every apply() re-enumerates the CRTCs so hotplugged monitors are covered.
class X11GammaBackend final : public GammaBackend {
public:
    explicit X11GammaBackend(QObject *parent = nullptr);
    ~X11GammaBackend() override;

    bool init();

    QString name() const override;
    bool available() const override;
    QString status() const override;
    void apply(const colortemp::Whitepoint &wp) override;
    void release() override;

private:
    void setAll(const colortemp::Whitepoint &wp);

    xcb_connection_t *m_conn = nullptr;
    int m_crtcCount = 0;
    bool m_touched = false;
};

#endif // QBAR_HAVE_XCB_RANDR
