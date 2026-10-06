#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build_switch.sh [debug|release|bringup] [--data-root PATH] [--no-data]

Configures the selected Nintendo Switch preset before building it, so the
command is safe to use in a fresh checkout with no CMake cache. Full-game
builds stage runtime data by default; use --no-data to build only the NRO.
EOF
}

mode=debug
data_root=
stage_data=1

while (($#)); do
    case "$1" in
        debug|release|bringup)
            mode=$1
            ;;
        --data-root)
            shift
            if (($# == 0)); then
                echo "error: --data-root requires a path" >&2
                usage >&2
                exit 2
            fi
            data_root=$1
            ;;
        --no-data)
            stage_data=0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$repo_root"

preset="switch-${mode}"
configure_args=(--preset "$preset")
if [[ -n "$data_root" ]]; then
    data_root=$(cd -- "$data_root" && pwd)
    configure_args+=("-DSWITCH_DATA_ROOT=$data_root")
fi

cmake "${configure_args[@]}"
cmake --build --preset "$preset"

if [[ "$mode" != bringup && $stage_data -eq 1 ]]; then
    cmake --build --preset "$preset" --target switch-data
fi
