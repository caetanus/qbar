#pragma once

#include "config.h"

class QQmlContext;
class QQmlEngine;
class QWindow;

// One-stop QML engine bootstrap for a bar: the themeicon image provider plus the
// injected JS globals (JsTimers, Http, JsonAsync, LocalStorage, Proc). Idempotent
// per engine — safe to call again on an engine that is already set up.
void installQbarEngineGlobals(QQmlEngine *engine);

// Expose the lazy capsule backends as context properties, but ONLY for the applets this
// bar's config actually uses — so an unconfigured applet's backend is never acquired
// (never built). Safe to re-run on a live config reload: acquire() returns the cached
// model, and newly-configured applets get their model exposed.
void exposeConfiguredModels(QQmlContext *context, const BarConfig &config, QWindow *window);
