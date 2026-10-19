#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qt_root="${QT_ROOT_DIR:-$HOME/Qt/6.8.3/gcc_64}"
qt_build_dir="output/qt-build-linux"
native_build_dir="output/native-build-linux"
package_dir="$repo_root/output/packages"
tool_dir="$repo_root/output/tools"
linuxdeploy_version="1-alpha-20250213-2"
linuxdeploy_qt_version="1-alpha-20250213-1"
linuxdeploy_sha256="4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a"
linuxdeploy_qt_sha256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

usage() {
    printf 'Usage: %s [--qt-root DIR] [--native-build-dir DIR] [--qt-build-dir DIR]\n' "$0"
}

while (($#)); do
    case "$1" in
        --qt-root|--native-build-dir|--qt-build-dir)
            if (($# < 2)); then usage >&2; exit 2; fi
            option="$1"
            value="$2"
            shift 2
            case "$option" in
                --qt-root) qt_root="$value" ;;
                --native-build-dir) native_build_dir="$value" ;;
                --qt-build-dir) qt_build_dir="$value" ;;
            esac
            ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

resolve_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$repo_root/$1" ;;
    esac
}

qt_root="$(resolve_path "$qt_root")"
native_build_dir="$(resolve_path "$native_build_dir")"
qt_build_dir="$(resolve_path "$qt_build_dir")"
mkdir -p "$package_dir" "$tool_dir"
"$repo_root/tools/build.sh" --qt-root "$qt_root" --native-build-dir "$native_build_dir" --qt-build-dir "$qt_build_dir"

download_pinned() {
    local name="$1" url="$2" expected="$3" destination="$tool_dir/$1" partial="$tool_dir/$1.download-$$"
    if [[ -e "$destination" ]]; then
        if ! printf '%s  %s\n' "$expected" "$destination" | sha256sum --check --status; then
            printf 'Pinned tool checksum mismatch: %s\n' "$destination" >&2
            exit 1
        fi
    else
        if ! curl --fail --location --silent --show-error --retry 3 --output "$partial" "$url"; then
            rm -f "$partial"
            exit 1
        fi
        if ! printf '%s  %s\n' "$expected" "$partial" | sha256sum --check --status; then
            rm -f "$partial"
            printf 'Downloaded tool checksum mismatch: %s\n' "$name" >&2
            exit 1
        fi
        mv "$partial" "$destination"
    fi
    chmod 0755 "$destination"
}

linuxdeploy="$tool_dir/linuxdeploy-x86_64.AppImage"
linuxdeploy_qt="$tool_dir/linuxdeploy-plugin-qt-x86_64.AppImage"
download_pinned "$(basename "$linuxdeploy")" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeploy_version/linuxdeploy-x86_64.AppImage" \
    "$linuxdeploy_sha256"
download_pinned "$(basename "$linuxdeploy_qt")" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$linuxdeploy_qt_version/linuxdeploy-plugin-qt-x86_64.AppImage" \
    "$linuxdeploy_qt_sha256"

work_dir="$package_dir/.appimage-build-$(date +%s)-$$"
app_dir="$work_dir/AppDir"
plugin_dir="$work_dir/plugin-bin"
icon_file="$work_dir/bottlemusic.png"
mkdir -p "$work_dir"
if ! command -v convert >/dev/null 2>&1; then
    printf 'ImageMagick convert is required to create the 256px desktop icon\n' >&2
    exit 1
fi
convert "$repo_root/assets/icons/icon.png" -resize 256x256 "$icon_file"
mkdir -p "$app_dir/usr/bin" "$app_dir/usr/share/applications" \
    "$app_dir/usr/share/icons/hicolor/256x256/apps" "$plugin_dir"
cp "$qt_build_dir/BottleMusicQt" "$app_dir/usr/bin/BottleMusicQt"
cp -L "$qt_build_dir/libEchoCAPI.so" "$app_dir/usr/bin/libEchoCAPI.so"
cp -a "$qt_build_dir/fonts" "$app_dir/usr/bin/fonts"
cp -a "$qt_build_dir/licenses" "$app_dir/usr/bin/licenses"
cp "$repo_root/LICENSE" "$app_dir/usr/bin/licenses/BottleMusic-LICENSE"
cp "$repo_root/desktop/qt/packaging/linux/bottlemusic.desktop" "$app_dir/usr/share/applications/bottlemusic.desktop"
cp "$icon_file" "$app_dir/usr/share/icons/hicolor/256x256/apps/bottlemusic.png"
ln -s "$repo_root/desktop/qt/packaging/linux/linuxdeploy-plugin-qt-wrapper.sh" "$plugin_dir/linuxdeploy-plugin-qt"

export LINUXDEPLOY_PLUGIN_QT_APPIMAGE="$linuxdeploy_qt"
export PATH="$plugin_dir:$qt_root/bin:$PATH"
export QML_SOURCES_PATHS="$repo_root/desktop/qt/qml"
export EXTRA_QT_MODULES="Multimedia"
export EXTRA_QT_PLUGINS="platforms/libqxcb.so;platforms/libqwayland-egl.so;platforms/libqwayland-generic.so;imageformats/libqsvg.so;multimedia/libffmpegmediaplugin.so;tls/libqopensslbackend.so"

sql_driver_backup="$work_dir/sql-driver-backup"
mkdir -p "$sql_driver_backup"
for sql_driver in "$qt_root"/plugins/sqldrivers/libqsql*.so; do
    [[ -e "$sql_driver" ]] || continue
    [[ "${sql_driver##*/}" == libqsqlite.so ]] && continue
    mv "$sql_driver" "$sql_driver_backup/"
done
restore_sql_drivers() {
    for sql_driver in "$sql_driver_backup"/*; do
        [[ -e "$sql_driver" ]] && mv "$sql_driver" "$qt_root/plugins/sqldrivers/"
    done
}
trap restore_sql_drivers EXIT

cd "$work_dir"
"$linuxdeploy" --appimage-extract-and-run \
    --appdir "$app_dir" \
    --executable "$app_dir/usr/bin/BottleMusicQt" \
    --library "$app_dir/usr/bin/libEchoCAPI.so" \
    --desktop-file "$repo_root/desktop/qt/packaging/linux/bottlemusic.desktop" \
    --icon-file "$icon_file" \
    --plugin qt \
    --output appimage

shopt -s nullglob
appimages=("$work_dir"/*.AppImage)
if ((${#appimages[@]} != 1)); then
    printf 'Expected one AppImage from linuxdeploy, found %d\n' "${#appimages[@]}" >&2
    exit 1
fi
appimage="$package_dir/BottleMusicQt-1.1.0-qt-preview-x86_64.AppImage"
install -m 0755 "${appimages[0]}" "$appimage"
sha256sum "$appimage" > "$appimage.sha256"
printf 'Created %s\n' "$appimage"
