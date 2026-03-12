#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: collect_reaper_coredump.sh [PID] [OUTPUT_FILE]

Captures metadata and a full non-interactive gdb backtrace for the latest
REAPER coredump, or for the specified PID if provided.

Examples:
  collect_reaper_coredump.sh
  collect_reaper_coredump.sh 66683
  collect_reaper_coredump.sh 66683 /tmp/reaper-coredump-66683.txt
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

pid="${1:-}"

if [[ -z "${pid}" ]]; then
  pid="$(
    coredumpctl list reaper --no-pager --no-legend 2>/dev/null \
      | awk 'NF >= 5 && $5 ~ /^[0-9]+$/ { last = $5 } END { print last }'
  )"
fi

if [[ -z "${pid}" ]]; then
  echo "No REAPER coredump found." >&2
  exit 1
fi

timestamp="$(date +%Y%m%d-%H%M%S)"
output="${2:-/tmp/reaper-coredump-${pid}-${timestamp}.txt}"

{
  echo "# GainPilot REAPER coredump report"
  echo "# Generated: $(date --iso-8601=seconds)"
  echo "# PID: ${pid}"
  echo
  echo "## coredumpctl info"
  coredumpctl info "${pid}" --no-pager
  echo
  echo "## gdb backtrace"
  coredumpctl debug "${pid}" \
    --debugger=gdb \
    --debugger-arguments="--batch -ex 'set pagination off' -ex 'thread apply all bt full' -ex 'quit'"
} > "${output}" 2>&1

echo "${output}"
