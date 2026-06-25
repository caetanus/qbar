#pragma once

#include <QString>

namespace qbar {

// Resolve an xkb layout description ("Portuguese (Brazil)", "English (US)") or a raw
// layout name ("br", "us") to the short code shown in the bar.
//
// The authoritative source is the xkb registry (libxkbregistry) — the canonical list
// of every layout and its code — so we never guess from the text (which truncates
// "Portuguese (Brazil)" to "po"). A small alias fallback covers raw codes/names the
// registry can't describe. Shared by every WM backend so sway, i3 and Hyprland agree.
QString keyboardLayoutCode(const QString &layoutNameOrDescription);

} // namespace qbar
