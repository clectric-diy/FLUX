#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIBRARIES_DIR="$SCRIPT_DIR/libraries"
FQBN="${FQBN:-arduino:megaavr:nona4809}"

usage() {
  cat <<EOF
Usage: $(basename "$0") <router|lunetta|sequencer|all>

Compiles Nexus Arduino variants using the bundled nexus-core library.

Environment overrides:
  FQBN   Arduino board FQBN (default: arduino:megaavr:nona4809)

Examples:
  $(basename "$0") router
  $(basename "$0") all
  FQBN=arduino:megaavr:nona4809 $(basename "$0") sequencer
EOF
}

require_arduino_cli() {
  if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "Error: arduino-cli is not installed or not on PATH." >&2
    echo "Install it first, then rerun this script." >&2
    exit 127
  fi
}

compile_variant() {
  local variant_name="$1"
  local sketch_dir="$SCRIPT_DIR/nexus-$variant_name"

  if [[ ! -d "$sketch_dir" ]]; then
    echo "Error: sketch directory not found: $sketch_dir" >&2
    exit 1
  fi

  echo "==> Compiling Nexus $variant_name"
  arduino-cli compile \
    --libraries "$LIBRARIES_DIR" \
    --fqbn "$FQBN" \
    "$sketch_dir"
}

main() {
  if [[ $# -ne 1 ]]; then
    usage
    exit 1
  fi

  require_arduino_cli

  case "$1" in
    router|lunetta|sequencer)
      compile_variant "$1"
      ;;
    all)
      compile_variant router
      compile_variant lunetta
      compile_variant sequencer
      ;;
    -h|--help|help)
      usage
      ;;
    *)
      usage
      exit 1
      ;;
  esac
}

main "$@"
