#!/bin/bash
# obtain and optionally verify Bench / signature
# if no reference is given, the output is deliberately limited to just the signature

STDOUT_FILE=$(mktemp)
STDERR_FILE=$(mktemp)

error() {
  echo "running bench for signature failed on line $1"
  echo "===== STDOUT ====="
  cat "$STDOUT_FILE"
  echo "===== STDERR ====="
  cat "$STDERR_FILE"
  rm -f "$STDOUT_FILE" "$STDERR_FILE"
  exit 1
}

trap 'error ${LINENO}' ERR

# obtain signature
eval "$RUN_PREFIX ./DON bench" > "$STDOUT_FILE" 2> "$STDERR_FILE" || error ${LINENO}
SIGNATURE=$(grep "Total nodes     : " "$STDERR_FILE" | awk '{print $4}')

rm -f "$STDOUT_FILE" "$STDERR_FILE"

if [ $# -gt 0 ]; then
   # compare to given reference
   if [ "$1" != "$SIGNATURE" ]; then
      if [ -z "$SIGNATURE" ]; then
         echo "No signature obtained from bench. Code crashed or assert triggered ?"
      else
         echo "signature mismatch: reference $1 obtained: $SIGNATURE."
      fi
      exit 1
   else
      echo "signature OK: $SIGNATURE."
   fi
else
   # just report signature
   echo $SIGNATURE
fi
