#!/usr/bin/env bash
# gen_voice.sh -- macOS 專用:say(TTS)→ afconvert(16k mono wav)→ wav2adpcm.py
# 產出:common/voice/voice.bin + common/components/poker_core/include/voice_ids.h(入版控)
set -euo pipefail
cd "$(dirname "$0")/../voice"
VOICE="${VOICE:-Samantha}"          # 單一音色,全部片段一致(產品 §6.1)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

while IFS=$'\t' read -r id text; do
  # 跳過空行與註解
  [ -z "${id:-}" ] && continue
  case "$id" in \#*) continue;; esac
  [ "$id" = "V_BEEP" ] && continue    # 由打包器合成
  say -v "$VOICE" -o "$T/$id.aiff" "$text"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$T/$id.aiff" "$T/$id.wav"
done < voice_list.tsv

python3 ../tools/wav2adpcm.py voice_list.tsv "$T" \
    -o voice.bin \
    --header ../components/poker_core/include/voice_ids.h
echo "voice.bin: $(stat -f%z voice.bin) bytes"
