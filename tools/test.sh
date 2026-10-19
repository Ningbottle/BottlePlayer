#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
native_build_dir="$repo_root/output/native-build-linux"
qt_build_dir="$repo_root/output/qt-build-linux"
run_native=true
run_qt=true
include_audio=false
mode=""

usage() {
    printf 'Usage: %s [--native-only|--qt-only] [--include-audio]\n' "$0"
}

while (($#)); do
    case "$1" in
        --native-only)
            if [[ -n "$mode" ]]; then usage >&2; exit 2; fi
            mode=native
            run_qt=false
            ;;
        --qt-only)
            if [[ -n "$mode" ]]; then usage >&2; exit 2; fi
            mode=qt
            run_native=false
            ;;
        --include-audio) include_audio=true ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

profile_root="$repo_root/output/test-profiles/linux-$(date +%s)-$$"
mkdir -p "$profile_root/tmp" "$profile_root/native-default"
export BOTTLE_TEST_TEMP_ROOT="$profile_root"
export TMPDIR="$profile_root/tmp"
export TEMP="$TMPDIR"
export TMP="$TMPDIR"
export ECHO_NATIVE_DATA_DIR="$profile_root/native-default"
export ECHO_NATIVE_SECRET_BACKEND=file
export QT_QPA_PLATFORM=offscreen

if [[ "$run_native" == true ]]; then
    ctest --test-dir "$native_build_dir" --output-on-failure --timeout 120 --no-tests=error
fi

if [[ "$run_qt" == true ]]; then
    qt_test_started="$(date +%s)"
    qt_args=(--test-dir "$qt_build_dir" --output-on-failure --timeout 120 --no-tests=error)
    if [[ "$include_audio" == false ]]; then qt_args+=(-E '^qt_audio_runtime$'); fi
    if ctest "${qt_args[@]}"; then
        :
    else
        status=$?
        for report in "$qt_build_dir"/Testing/qt_*.txt; do
            if [[ -f "$report" ]] && (( $(stat -c %Y "$report") >= qt_test_started )); then
                printf '\n--- Fresh QtTest report: %s ---\n' "$report"
                sed -n '1,260p' "$report"
            fi
        done
        exit "$status"
    fi
fi
