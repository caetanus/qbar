# Known Issues

## Dock

- The Dock still has a visible flicker when a window leaves the dock. The current
  implementation keeps the layer-shell surface stable and delays the reserved
  width shrink, but the exit path still needs more work.

## Popup memory growth (fixed by shell reuse)

- Each popup open/close used to permanently grow the process by a few MB
  (~4MB with the bundled themes). Root cause: Qt Quick creates new graphics
  pipelines for the layer render targets that `text-shadow` effects require on
  every open, and its window-level pipeline cache never evicts them (measured
  with heaptrack; `QSGBatchRenderer::ensurePipelineState` / `QSGRhiLayer`
  retention). Destroying the overlay window doesn't return the memory either.
- **Fix**: popups now park their shell (hidden) on close and revive it on the
  next open, and the backdrop overlay is hidden instead of destroyed — same
  items in the same living window, so after each popup's first open no new
  pipelines are created (soak-verified flat across hundreds of cycles).
- An upstream Qt report on pipeline-cache eviction is still tracked; other
  Qt Quick apps with dynamic layer-effect content hit the same retention.
