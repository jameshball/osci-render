#!/usr/bin/env bash
set -euo pipefail

task="${1:?Usage: mega.sh <install|upload-pluginval-logs|upload-binaries> [args...]}"
shift

require_credentials() {
  if [ -z "${MEGA_USERNAME:-}" ] || [ -z "${MEGA_PASSWORD:-}" ]; then
    echo "ERROR: MEGA_USERNAME and MEGA_PASSWORD must be set" >&2
    exit 1
  fi
}

install_megacmd() {
  if command -v mega-login >/dev/null 2>&1; then
    return 0
  fi

  case "${OS:-${RUNNER_OS:-}}" in
    linux|Linux)
      wget -q https://mega.nz/linux/repo/xUbuntu_24.04/amd64/megacmd-xUbuntu_24.04_amd64.deb
      sudo apt-get update -y >/dev/null
      sudo apt-get install -y ./megacmd-xUbuntu_24.04_amd64.deb >/dev/null
      ;;
    mac|macOS)
      brew install --cask megacmd
      if [ -n "${GITHUB_PATH:-}" ]; then
        echo "/Applications/MEGAcmd.app/Contents/MacOS" >> "$GITHUB_PATH"
      fi
      ;;
    *)
      echo "ERROR: unsupported OS for MEGAcmd install: ${OS:-${RUNNER_OS:-unknown}}" >&2
      exit 1
      ;;
  esac
}

upload_pluginval_logs() {
  local project="${1:?project is required}"
  local project_version="${2:?project version is required}"
  local os_artifact="${3:?OS artifact is required}"
  local log_dir="${PLUGINVAL_LOG_DIR:-bin/pluginval-logs}"
  local dest="/$project/$project_version/pluginval-logs/$os_artifact"

  require_credentials
  mega-login "$MEGA_USERNAME" "$MEGA_PASSWORD" || true

  if [ -d "$log_dir" ]; then
    find "$log_dir" -type f -size +0c -print0 2>/dev/null | while IFS= read -r -d '' file; do
      echo "Uploading $file -> $dest"
      mega-put -c "$file" "$dest/" || echo "WARNING: failed to upload $file"
    done
  fi

  mega-logout || true
}

upload_binaries() {
  local project="${1:?project is required}"
  local project_version="${2:?project version is required}"
  local variant="${3:?variant is required}"
  local root="/$project/$project_version"
  local dest="$root"

  if [ "$project" != "sosci" ]; then
    dest="$root/$variant"
  fi

  require_credentials
  mega-login "$MEGA_USERNAME" "$MEGA_PASSWORD"

  ls -1 bin || true
  shopt -s nullglob
  for file in bin/*; do
    if [ -f "$file" ]; then
      echo "Uploading $file -> $dest"
      mega-put -c "$file" "$dest/"
    fi
  done

  mega-logout
}

case "$task" in
  install)
    install_megacmd
    ;;
  upload-pluginval-logs)
    upload_pluginval_logs "$@"
    ;;
  upload-binaries)
    upload_binaries "$@"
    ;;
  *)
    echo "ERROR: unknown MEGAcmd task: $task" >&2
    exit 1
    ;;
esac