#!/bin/sh

# Download commands with a 5min time-out to ensure things fail if the server stalls
wget_or_curl=$( (command -v wget >/dev/null 2>&1 && echo "wget -qO- --timeout=300 --tries=1") ||
                (command -v curl >/dev/null 2>&1 && echo "curl -skL --max-time 300"))

sha256sum=$( (command -v shasum >/dev/null 2>&1 && echo "shasum -a 256") ||
             (command -v sha256sum >/dev/null 2>&1 && echo "sha256sum"))

if [ -z "$sha256sum" ]; then
  >&2 echo "sha256sum not found, NNUE files will be assumed valid."
fi

get_nnue_filename() {
  grep "$1" evaluate.h | grep "#define" | sed "s/.*\(nn-[a-z0-9]\{12\}.nnue\).*/\1/"
}

validate_network() {
  # If no sha256sum command is available, assume the file is valid.
  if [ -z "$sha256sum" ]; then
    return 0
  fi
  if [ -f "$1" ]; then
    if [ "$1" != "nn-$($sha256sum "$1" | cut -c 1-12).nnue" ]; then
      rm -f "$1"
      return 1
    fi
  fi
}

fetch_network() {
  filename="$(get_nnue_filename "$1")"

  if [ -z "$filename" ]; then
    >&2 echo "NNUE file name not found for: $1"
    return 1
  fi

  if [ -f "$filename" ]; then
    if validate_network "$filename"; then
      echo "Existing $filename validated, skipping download"
      return
    else
      echo "Removing invalid NNUE file: $filename"
    fi
  fi

  if [ -z "$wget_or_curl" ]; then
    >&2 echo "Neither wget or curl is installed. Install one of these tools to download NNUE files automatically."
    exit 1
  fi

  for url in \
    "https://tests.stockfishchess.org/api/nn/$filename" \
    "https://github.com/official-stockfish/networks/raw/master/$filename"; do
    echo "Downloading from $url ..."
    if $wget_or_curl "$url" >"$filename"; then
      if validate_network "$filename"; then
        echo "Successfully validated $filename"
      else
        rm -f $filename
        echo "Downloaded $filename is invalid, and has been removed."
        continue
      fi
    else
      echo "Failed to download from $url"
    fi
    if [ -f "$filename" ]; then
      return
    fi
  done

  # Download was not successful in the loop, return false.
  >&2 echo "Failed to download $filename"
  return 1
}

for net in \
    BigEvalFileDefaultName \
    SmallEvalFileDefaultName; do
  fetch_network "$net" || exit 1
done
