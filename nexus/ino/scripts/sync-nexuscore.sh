#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_LIB_DIR="$(cd -- "$SCRIPT_DIR/../libraries/nexus-core" && pwd)"

resolve_user_dir() {
  if command -v arduino-cli >/dev/null 2>&1; then
    local dir
    dir="$(arduino-cli config dump 2>/dev/null | awk '/^[[:space:]]*user:[[:space:]]*/ {print $2; exit}')"
    if [[ -n "${dir:-}" ]]; then
      printf '%s\n' "$dir"
      return 0
    fi
  fi

  # Fallback used by Arduino on macOS/Linux when config is missing.
  # Some VS Code build environments may not export HOME.
  local home_dir
  if [[ -n "${HOME:-}" ]]; then
    home_dir="$HOME"
  else
    home_dir="$(cd ~ && pwd)"
  fi

  printf '%s\n' "$home_dir/Documents/Arduino"
}

USER_DIR="$(resolve_user_dir)"
TARGET_LIB_DIR="$USER_DIR/libraries/nexus-core"

mkdir -p "$USER_DIR/libraries"
rm -rf "$TARGET_LIB_DIR"
cp -R "$SOURCE_LIB_DIR" "$TARGET_LIB_DIR"

echo "[Nexus] Synced bundled library to: $TARGET_LIB_DIR"
