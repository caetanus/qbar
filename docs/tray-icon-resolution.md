# qbar

## Tray icon resolution

### Architecture

```
SNI item (DBus)
  → StatusNotifierModel::refreshItem()
    → iconCanResolve() checks: file path → QIcon::fromTheme → custom path
    → builds iconSource: "image://themeicon/" + iconName  OR  pixmap data URL
  → Tray.qml Image { source: iconSource }
    → ThemeIconProvider::requestPixmap() (in applethost.cpp)
      → QFileInfo::exists → QIcon::fromTheme → findIconFile → application-x-executable
```

### Resolution order

1. `$HOME/.local/share/icons` → `$HOME/.icons` → `/usr/share/icons` (Qt theme search paths)
2. GTK icon theme (`QIcon::themeName()`) has priority over `hicolor` fallback
3. Dark/light mode handled by the icon theme itself
4. Monochromatic (`-symbolic`) icons: apply contrast tint as final step

### Pixmap byte order

DBus `IconPixmap` data is `[A,R,G,B]`. Converted to `[R,G,B,A]` for `QImage::Format_RGBA8888`.
Largest pixmap (by area) is selected, matching waybar's `extractPixBuf`.

### Troubleshooting: regressions from 2026-06-11

**Symptom:** All tray icons show gear (application-x-executable) or wrong colors.

**Root causes found:**

1. **Removing `iconCanResolve` check in `refreshItem`**
   - `iconCanResolve` exists to verify the icon name can actually be found before using `image://themeicon/` URL.
   - Without it, the provider returns fallback gear icon, and the pixmap data URL fallback is never reached.
   - **Rule:** Never remove a fallback without understanding why it exists.

2. **Applying `contrastColor` tint to all icons**
   - `tintColor` in the URL (`parts[1]`) must remain empty for non-symbolic icons.
   - Setting it to a valid color causes ALL icons to be tinted, destroying themed colors.
   - **Rule:** Tint only `-symbolic` icons, and only as the final step.

3. **Changing URL format + parsing + provider simultaneously**
   - The URL format `a||b|c|d` with positional `|`-separated parts is fragile.
   - Changing part positions breaks parsing silently.
   - **Rule:** One change at a time. Test between each.

**Checklist before modifying tray icon code:**
- [ ] Does `iconCanResolve` still guard the `image://themeicon/` URL?
- [ ] Is `tintColor` (parts[1]) empty for non-symbolic icons?
- [ ] Does pixmap data URL fallback still work when icon name can't be resolved?
- [ ] Are URL part indices consistent between `refreshItem` and `ThemeIconProvider::requestPixmap`?
- [ ] Test with: Bluetooth (themed icon), Dropbox (IconPixmap), WhatsApp/WhatsDesk (IconThemePath)
