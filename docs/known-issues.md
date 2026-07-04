# Known Issues

## Dock

- The Dock still has a visible flicker when a window leaves the dock. The current
  implementation keeps the layer-shell surface stable and delays the reserved
  width shrink, but the exit path still needs more work.

## Popup memory growth

- Each popup open/close permanently grows the process by a few MB (~4MB with the
  bundled themes). Root cause: Qt Quick creates new graphics pipelines for the
  layer render targets that `text-shadow` effects require on every open, and its
  window-level pipeline cache never evicts them (measured with heaptrack;
  `QSGBatchRenderer::ensurePipelineState` / `QSGRhiLayer` retention).
- **Theme knob**: roughly half of the growth comes from `text-shadow` in popup
  content — a theme that omits it pays proportionally less. The rest is
  per-open resource churn independent of theming.
- Restarting the bar reclaims everything. A popup-reuse mode (keeping shells
  alive across open/close, which also removes the growth entirely without any
  visual change) and an upstream Qt report are the tracked fixes.
