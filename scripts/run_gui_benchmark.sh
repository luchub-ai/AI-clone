#!/usr/bin/env bash
# run_gui_benchmark.sh
#
# Goi build/run_eval_gui voi ColabClient tro toi Ollama server dang chay
# tren Kaggle (qua cloudflared tunnel). Khong dung .env vi run_eval_gui
# chi doc bien moi truong that (std::getenv), khong load file .env.
#
# Usage:
#   ./run_gui_benchmark.sh https://xxxx-yyyy.trycloudflare.com
#   ./run_gui_benchmark.sh https://xxxx-yyyy.trycloudflare.com qwen3-vl:8b-instruct
#   ./run_gui_benchmark.sh https://xxxx-yyyy.trycloudflare.com qwen3-vl:8b-instruct --max-tokens=4096

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <tunnel_url> [model] [extra run_eval_gui flags...]" >&2
    echo "Vi du: $0 https://xxxx-yyyy.trycloudflare.com qwen3-vl:8b" >&2
    exit 1
fi

TUNNEL_URL="$1"
MODEL="${2:-qwen3-vl:8b}"
shift
[ $# -ge 1 ] && shift  # bo model neu co, con lai la extra flags
EXTRA_FLAGS=("$@")

BINARY="./build/GUI_Agent"
if [ ! -x "$BINARY" ]; then
    echo "Khong tim thay $BINARY - kiem tra da build (cmake --build build) va dang dung dung thu muc goc project chua." >&2
    exit 1
fi

echo "== Ping thu tunnel truoc khi chay batch (tranh chay het tasks moi phat hien server chet) =="
if ! curl -fsS -m 10 "${TUNNEL_URL%/}/api/tags" > /dev/null; then
    echo "CANH BAO: khong ping duoc ${TUNNEL_URL}/api/tags - kiem tra lai Kaggle notebook con chay khong." >&2
    exit 1
fi
echo "Tunnel OK."

echo "== Chay benchmark GUI agent =="
echo "client=colab  base-url=${TUNNEL_URL}  model=${MODEL}"

"$BINARY" \
    --client=colab \
    --base-url="$TUNNEL_URL" \
    --model="$MODEL" \
    "${EXTRA_FLAGS[@]}"

echo
echo "Xong. Xem ket qua trong benchmark_guiagent/gui_results/summary.json"
