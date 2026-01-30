#!/usr/bin/env bash
# Lightweight perft runner for tests/perft/*.in
# Usage: ./tests/perft_run.sh /path/to/engine_binary
#
# The script tries a few ways to invoke a perft on the engine:
#  - direct CLI: 'engine go perft "<FEN>" <depth>'
#    - CLI flag: 'engine --perft "<FEN>" <depth>'
#    - CLI flag: 'engine -perft "<FEN>" <depth>'
#  - UCI mode: send 'position fen ...' then 'go perft <depth>' via stdin (many engines accept this)
#
# If your engine uses a different perft interface, adapt the invocation block below.

set -euo pipefail

ENGINE="$1"
if [[ -z "$ENGINE" || ! -x "$ENGINE" ]]; then
  echo "Usage: $0 /path/to/engine_binary (must be executable)"
  exit 2
fi

IN_DIR="tests/perft"
shopt -s nullglob

#files=("$IN_DIR"/*.in)

files=()

# Put start.in first if it exists
if [[ -f "$IN_DIR/start.in" ]]; then
  files+=("$IN_DIR/start.in")
fi

# Append all other .in files except start.in
for f in "$IN_DIR"/*.in; do
  [[ $(basename "$f") == "start.in" ]] && continue
  files+=("$f")
done

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No .in files found in $IN_DIR"
  exit 1
fi

run_direct() {
  local fen="$1" depth="$2"
  # Try common CLI forms
  if output="$("$ENGINE" perft "$fen" "$depth" 2>&1 | tr -d '\000')"; then
    echo "$output"
    return 0
  fi
  if output="$("$ENGINE" --perft "$fen" "$depth" 2>&1 | tr -d '\000')"; then
    echo "$output"
    return 0
  fi
  if output="$("$ENGINE" -perft "$fen" "$depth" 2>&1 | tr -d '\000')"; then
    echo "$output"
    return 0
  fi
  return 1
}

run_uci() {
  local fen="$1" depth="$2"
  # Send UCI-style commands on stdin; many engines accept an extended "go perft" command.
  # If your engine doesn't support this, adapt to its interface.
  #printf "uci\nisready\nposition fen %s\ngo perft %d\nquit\n" "$fen" "$depth" | "$ENGINE" 2>&1 | tr -d '\000'

  local uci_commands
  read -r -d '' uci_commands <<EOF
uci
isready
position fen $fen
go perft $depth
quit
EOF

  echo "[debug] UCI commands:" >&2
  echo "$uci_commands" >&2

  printf "%s\n" "$uci_commands" | "$ENGINE" 2>&1 | tr -d '\000'
  return $?
}

echo "Using engine: $ENGINE"
echo

overall_fail=0

for f in "${files[@]}"; do
  echo "==> Test file: $f"
  # Parse file: first non-empty, non-comment line is FEN; line with "depth N" supplies depth.
  fen=""
  depth=""
  while IFS= read -r line || [[ -n $line ]]; do
    # Trim leading/trailing whitespace
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" ]] && continue
    [[ "${line:0:1}" = "#" ]] && continue
    if [[ -z "$fen" ]]; then
      fen="$line"
      continue
    fi
    if [[ "$line" =~ depth[[:space:]]+([0-9]+) ]]; then
      depth="${BASH_REMATCH[1]}"
    fi
  done < "$f"

  if [[ -z "$fen" || -z "$depth" ]]; then
    echo "  >> Invalid test file format (need FEN line and 'depth N')"
    overall_fail=1
    continue
  fi

  echo "  FEN: $fen"
  echo "  depth: $depth"
  echo

#  echo "  Trying direct CLI invocation..."
#  if out="$(run_direct "$fen" "$depth" )"; then
#    echo "  [direct] Output:"
#    echo "$out" | sed 's/^/    /'
#    echo
#    continue
#  fi

  echo "  Trying UCI-style stdin invocation..."
  if out="$(run_uci "$fen" "$depth" )"; then
    echo "  [uci] Output:"
    echo "$out" | sed 's/^/    /'
    echo
    continue
  fi

  echo "  Engine did not respond to any attempted perft invocation for this test."
  echo "  You may need to adapt tests/perft_run.sh to match your engine's perft interface."
  overall_fail=1
done

if [[ $overall_fail -ne 0 ]]; then
  echo "One or more perft tests failed or were unsupported by the engine interface."
  exit 1
fi

echo "All perft inputs processed (engine supported at least one invocation style)."
exit 0
