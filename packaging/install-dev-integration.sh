#!/usr/bin/env bash
# Dev-only desktop integration: install the ses icon + desktop entry so the
# shell/taskbar resolves the app icon when running from the build directory
# (required on Wayland; X11/XWayland already get it via setWindowIcon).
# P6 packaging installs the same files inside the AppImage (DESIGN §12).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
icons="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
mkdir -p "$apps" "$icons"
cp "$here/ses.desktop" "$apps/"
cp "$root/assets/icon.svg" "$icons/ses.svg"
update-desktop-database "$apps" 2>/dev/null || true
gtk-update-icon-cache "${icons%/apps}" 2>/dev/null || true
echo "installed: $apps/ses.desktop, $icons/ses.svg"