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

DIRECTORY="tests/perft"
shopt -s nullglob

#FILES=("$DIRECTORY"/*.in)

FILES=()

# Put start.in first if it exists
if [[ -f "$DIRECTORY/start.in" ]]; then
  FILES+=("$DIRECTORY/start.in")
fi

# Append all other .in files except start.in
for f in "$DIRECTORY"/*.in; do
  [[ ${f##*/} == "start.in" ]] && continue
  FILES+=("$f")
done

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "No .in files found in $DIRECTORY"
  exit 1
fi

run_direct() {
  local FEN="$1" DEPTH="$2"
  # Try common CLI forms
  if output="$("$ENGINE" perft "$FEN" "$DEPTH" 2>&1 | tr -d '\000')"; then
    echo "$output"
    return 0
  fi
  if output="$("$ENGINE" --perft "$FEN" "$DEPTH" 2>&1 | tr -d '\000')"; then
    echo "$output"
    return 0
  fi
  if output="$("$ENGINE" -perft "$FEN" "$DEPTH" 2>&1 | tr -d '\000')"; then
    echo "$output"
    return 0
  fi
  return 1
}

run_uci() {
  local FEN="$1" DEPTH="$2"
  # Send UCI-style commands on stdin; many engines accept an extended "go perft" command.
  # If your engine doesn't support this, adapt to its interface.
  #printf "uci\nisready\nposition fen %s\ngo perft %d\nquit\n" "$FEN" "$DEPTH" | "$ENGINE" 2>&1 | tr -d '\000'

  local UCI_COMMANDS
  read -r -d '' UCI_COMMANDS <<EOF
uci
isready
position fen $FEN
go perft $DEPTH
quit
EOF

  echo "[debug] UCI commands:" >&2
  echo "$UCI_COMMANDS" >&2

  printf "%s\n" "$UCI_COMMANDS" | "$ENGINE" 2>&1 | tr -d '\000'
  return $?
}

echo "Using engine: $ENGINE"
echo

TESTS_FAILED=0

for f in "${FILES[@]}"; do
  echo "==> Test file: $f"
  # Parse file: first non-empty, non-comment line is FEN; line with "depth N" supplies depth.
  FEN=""
  DEPTH=""
  while IFS= read -r line || [[ -n $line ]]; do
    # Trim leading/trailing whitespace
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" ]] && continue
    [[ "${line:0:1}" = "#" ]] && continue
    if [[ -z "$FEN" ]]; then
      FEN="$line"
      continue
    fi
    if [[ "$line" =~ depth[[:space:]]+([0-9]+) ]]; then
      DEPTH="${BASH_REMATCH[1]}"
    fi
  done < "$f"

  if [[ -z "$FEN" || -z "$DEPTH" ]]; then
    echo "  >> Invalid test file format (need FEN line and 'depth N')"
    TESTS_FAILED=1
    continue
  fi

  echo "  FEN: $FEN"
  echo "  depth: $DEPTH"
  echo

#  echo "  Trying direct CLI invocation..."
#  set +e
#  out="$(run_direct "$FEN" "$DEPTH" 2>&1)"
#  status=$?
#  set -e
#  if (( status == 0 )); then
#    echo "  [direct] Output:"
#    echo "$out" | sed 's/^/    /'
#    echo
#    continue
#  fi

  echo "  Trying UCI-style stdin invocation..."
  set +e
  out="$(run_uci "$FEN" "$DEPTH" 2>&1)"
  status=$?
  set -e
  if (( status == 0 )); then
    echo "  [uci] Output:"
    echo "$out" | sed 's/^/    /'
    echo
    continue
  fi

  echo "  Engine did not respond to any attempted perft invocation for this test."
  echo "  You may need to adapt tests/perft_run.sh to match your engine's perft interface."
  TESTS_FAILED=1
done

if [[ $TESTS_FAILED -ne 0 ]]; then
  echo "One or more perft tests failed or were unsupported by the engine interface."
  exit 1
fi

echo "All perft inputs processed (engine supported at least one invocation style)."
exit 0
