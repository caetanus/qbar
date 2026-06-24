# qbar — prototype AUR package

A local `PKGBUILD` for installing qbar on Arch **before** it lands on the AUR.
It's a `-git` package: it builds straight from the GitHub `master` branch and
pulls in *everything* qbar needs at runtime as hard dependencies (SVG plugin,
icon theme, fonts, evolution-data-server daemon, both display backends) — so
`makepkg` here never leaves you with the "builds, then half the applets silently
break" problem.

## Install

From this directory:

```bash
makepkg -si        # build + install (also installs missing deps)
```

Or with an AUR helper pointed at the local dir:

```bash
yay -B .           # build a local PKGBUILD directory
# then: sudo pacman -U qbar-git-*.pkg.tar.zst
```

Updating later just means re-running `makepkg -si` — the `pkgver()` tracks the
latest `master` commit.

## When the real AUR package exists

This is a placeholder. Once published, drop this directory and use
`yay -S qbar-git` (or `qbar` for a tagged release) from the AUR proper.
