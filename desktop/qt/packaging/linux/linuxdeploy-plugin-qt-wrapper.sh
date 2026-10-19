#!/usr/bin/env bash
set -euo pipefail
: "${LINUXDEPLOY_PLUGIN_QT_APPIMAGE:?}"
exec "$LINUXDEPLOY_PLUGIN_QT_APPIMAGE" --appimage-extract-and-run "$@"
