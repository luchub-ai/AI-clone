#!/usr/bin/env bash
# start_agent.sh - chay 1 lenh duy nhat thay vi phai export tay + tu bat
# chromedriver moi lan. Dat file nay o thu muc scripts.
#
# Cach dung:
#   chmod +x start_agent.sh
#   ./start_agent.sh                       # chay voi tasks.json mac dinh
#   ./start_agent.sh --tasks=benchmark/tasks.json --out=benchmark/results
#
# Moi tham so truyen vao script se duoc forward thang cho ./build/run_eval.
#
# Script nay tu lo CA setup lan dau (cai Chrome for Testing + chromedriver,
# build project) LAN chay lai moi lan sau - danh cho nguoi cham bai chi
# clone repo ve va muon go dung 1 lenh duy nhat. Buoc cai dat lan dau can
# sudo (qua setup_chromedriver.sh) nen se hoi mat khau sudo neu chua cai.
set -euo pipefail
cd "$(dirname "$0")/.."

# 1. Nap .env cung thu muc (set -a de moi bien duoc export tu dong,
#    khong can khai export tung dong).
if [[ -f .env ]]; then
    set -a
    source .env
    set +a
else
    echo "Canh bao: khong tim thay .env o $(pwd), dung bien moi truong hien co (neu co)."
fi

if [[ -z "${TAVILY_API_KEY:-}" ]]; then
    echo "Canh bao: TAVILY_API_KEY dang rong - cac task dung WebSearchTool se fail." \
         "Sua .env voi key rieng cua ban neu can task do chay duoc."
fi

# 2. Cai Chrome for Testing + chromedriver NEU CHUA CO (lan dau tren may
#    moi). Kiem tra ca 2 dieu kien: chromedriver co trong PATH, VA
#    BROWSER_BINARY_PATH (tu .env, dung $HOME nen tu dung tren moi may)
#    tro toi 1 file thuc su ton tai.
if ! command -v chromedriver >/dev/null 2>&1 || [[ ! -x "${BROWSER_BINARY_PATH:-/nonexistent}" ]]; then
    echo "Chua tim thay chromedriver/Chrome for Testing - dang chay setup_chromedriver.sh (can sudo)..."
    bash setup_chromedriver.sh
else
    echo "chromedriver + Chrome for Testing da san sang."
fi

# 3. Build project NEU CHUA BUILD (lan dau tren may moi).
if [[ ! -x ./build/run_eval ]]; then
    if ! command -v cmake >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1; then
        echo "Thieu cmake hoac g++." >&2
        echo "  Arch:            sudo pacman -S cmake base-devel" >&2
        echo "  Debian/Ubuntu:   sudo apt-get install cmake build-essential" >&2
        exit 1
    fi
    echo "Chua thay ./build/run_eval - dang build project..."
    cmake -B build -S .
    cmake --build build
else
    echo "Project da build san."
fi

# 4. Kiem tra chromedriver da chay chua (qua endpoint /status), neu chua
#    thi tu bat nen o dung port lay tu CHROMEDRIVER_URL.
DRIVER_URL="${CHROMEDRIVER_URL:-http://127.0.0.1:9515}"
DRIVER_PORT="${DRIVER_URL##*:}"

if curl -s -o /dev/null -w "%{http_code}" "${DRIVER_URL}/status" 2>/dev/null | grep -q "200"; then
    echo "chromedriver da chay san tai ${DRIVER_URL}."
else
    echo "chromedriver chua chay, dang khoi dong nen tai port ${DRIVER_PORT}..."
    nohup chromedriver --port="${DRIVER_PORT}" > /tmp/chromedriver.log 2>&1 &
    disown
    # Doi toi da 5s cho chromedriver san sang truoc khi chay agent.
    for _ in {1..10}; do
        if curl -s -o /dev/null -w "%{http_code}" "${DRIVER_URL}/status" 2>/dev/null | grep -q "200"; then
            break
        fi
        sleep 0.5
    done
    echo "chromedriver dang chay (log: /tmp/chromedriver.log)."
fi

# 5. Chay agent, forward toan bo tham so dong lenh.
exec ./build/run_eval "$@"