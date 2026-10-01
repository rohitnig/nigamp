#!/bin/bash
# Installs a desktop entry for this checkout's nigamp build (current user only).
# GNOME matches the --gui window to it via StartupWMClass, so the window gets its own
# Alt+Tab / dock entry and icon instead of being grouped with the terminal that launched it.
# Remove with: ./install_desktop_entry.sh --uninstall

set -e
REPO="$(cd "$(dirname "$0")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ENTRY="$APPS/nigamp.desktop"

if [ "$1" = "--uninstall" ]; then
    rm -f "$ENTRY"
    echo "Removed $ENTRY"
    exit 0
fi

if [ ! -x "$REPO/build/nigamp" ]; then
    echo "Build nigamp first (./build.sh); $REPO/build/nigamp not found" >&2
    exit 1
fi

mkdir -p "$APPS"
cat > "$ENTRY" <<EOF
[Desktop Entry]
Type=Application
Name=Nigamp
Comment=Lightweight shuffle music player
Exec="$REPO/build/nigamp" --gui
Icon=$REPO/resources/nigamp.png
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=nigamp
EOF
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null || true
echo "Installed $ENTRY"
