# Known Issues

## Dock

- The Dock still has a visible flicker when a window leaves the dock. The current
  implementation keeps the layer-shell surface stable and delays the reserved
  width shrink, but the exit path still needs more work.
