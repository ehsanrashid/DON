#!/bin/bash
# obtain and optionally verify Bench / signature
# if no reference is given, the output is deliberately limited to just the signature

#StdoutFile=$(mktemp)
#StderrFile=$(mktemp)

error() {
  echo "running bench for signature failed on line $1"
  #echo "===== STDOUT ====="
  #cat "$StdoutFile"
  #echo "===== STDERR ====="
  #cat "$StderrFile"
  #rm -f "$StdoutFile" "$StderrFile"
  exit 1
}

trap 'error ${LINENO}' ERR

# obtain signature
signature=`eval "$WINE_PATH ./DON bench 2>&1" | grep "Total nodes     : " | awk '{print $4}'`

#eval "$WINE_PATH ./DON bench" > "$StdoutFile" 2> "$StderrFile" || error ${LINENO}
#signature=$(grep "Total nodes     : " "$StderrFile" | awk '{print $4}')

#rm -f "$StdoutFile" "$StderrFile"

if [ $# -gt 0 ]; then
   # compare to given reference
   if [ "$1" != "$signature" ]; then
      if [ -z "$signature" ]; then
         echo "No signature obtained from bench. Code crashed or assert triggered ?"
      else
         echo "signature mismatch: reference $1 obtained: $signature ."
      fi
      exit 1
   else
      echo "signature OK: $signature"
   fi
else
   # just report signature
   echo $signature
fi
