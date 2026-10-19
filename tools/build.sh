#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qt_root="${QT_ROOT_DIR:-$HOME/Qt/6.8.3/gcc_64}"
native_build_dir="output/native-build-linux"
qt_build_dir="output/qt-build-linux"
clean_build=false

usage() {
    printf 'Usage: %s [--qt-root DIR] [--native-build-dir DIR] [--qt-build-dir DIR] [--clean]\n' "$0"
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
        --clean) clean_build=true; shift ;;
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
if [[ ! -f "$qt_root/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    printf 'Qt 6 CMake package not found under %s\n' "$qt_root" >&2
    exit 1
fi

cmake -S "$repo_root/native" -B "$native_build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
build_args=(--parallel "${BUILD_JOBS:-2}")
if [[ "$clean_build" == true ]]; then build_args+=(--clean-first); fi
cmake --build "$native_build_dir" "${build_args[@]}"
cmake -S "$repo_root/desktop/qt" -B "$qt_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt_root" \
    -DECHO_NATIVE_DIR="$native_build_dir"
cmake --build "$qt_build_dir" "${build_args[@]}"
