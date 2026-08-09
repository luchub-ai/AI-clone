#!/usr/bin/env bash
# Cai Chrome for Testing + chromedriver KHOP version voi nhau (khong dung
# apt install chromium-browser roi tim chromedriver rieng - de bi lech
# version, chromedriver se tu choi ket noi). Nguon: Chrome for Testing la
# kenh phan phoi chinh thuc cua Google danh rieng cho automation/testing.
#
# Chay 1 lan:  bash setup_chromedriver.sh
set -euo pipefail

for cmd in curl unzip; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Thieu lenh '$cmd'. Tren Arch: sudo pacman -S $cmd" >&2
        echo "Tren Debian/Ubuntu: sudo apt-get install $cmd" >&2
        exit 1
    fi
done

INSTALL_DIR="$HOME/chrome-for-testing"
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

echo "Dang lay version Stable moi nhat..."
VERSION=$(curl -s https://googlechromelabs.github.io/chrome-for-testing/LATEST_RELEASE_STABLE)
echo "Chrome for Testing (Stable): $VERSION"

curl -sO "https://storage.googleapis.com/chrome-for-testing-public/${VERSION}/linux64/chrome-linux64.zip"
curl -sO "https://storage.googleapis.com/chrome-for-testing-public/${VERSION}/linux64/chromedriver-linux64.zip"

unzip -oq chrome-linux64.zip
unzip -oq chromedriver-linux64.zip

echo "Cai dependency he thong cho Chrome (can sudo)..."
if command -v pacman >/dev/null 2>&1; then
    # Arch khong co "deb.deps" (danh sach goi .deb) nen phai tu liet ke
    # ten goi pacman tuong duong (thu vien shared ma Chrome for Testing can).
    echo "  -> Phat hien pacman (Arch Linux)."
    sudo pacman -Sy --needed --noconfirm \
        nss nspr atk at-spi2-core at-spi2-atk cups libcups \
        libdrm libxkbcommon libxcomposite libxdamage libxfixes libxrandr \
        mesa alsa-lib pango cairo gtk3 gdk-pixbuf2 dbus expat glib2 \
        libxss libxtst
elif command -v apt-get >/dev/null 2>&1; then
    # Debian/Ubuntu: dung dung file deb.deps di kem ban tai ve, khop chinh
    # xac version Chrome for Testing vua lay.
    echo "  -> Phat hien apt (Debian/Ubuntu)."
    sudo apt-get update -qq
    while read -r pkg; do
        sudo apt-get satisfy -y --no-install-recommends "$pkg" >/dev/null
    done < chrome-linux64/deb.deps
else
    echo "  -> Khong nhan dien duoc package manager (khong phai pacman/apt)." >&2
    echo "     Tu cai dependency thu cong; xem chrome-linux64/deb.deps de tham khao" >&2
    echo "     ten thu vien can thiet (nss, glib2, gtk3, alsa-lib, mesa, ...)." >&2
fi

sudo mv -f chromedriver-linux64/chromedriver /usr/local/bin/chromedriver
sudo chmod +x /usr/local/bin/chromedriver

echo ""
echo "=== Xong. Kiem tra ==="
chromedriver --version
"$INSTALL_DIR/chrome-linux64/chrome" --version

echo ""
echo "Duong dan Chrome binary (truyen vao constructor WebBrowserTool o tham"
echo "so thu 2, hoac cho vao CliOptions/getenv giong tavily_api_key):"
echo "  $INSTALL_DIR/chrome-linux64/chrome"
echo ""
echo "Truoc khi chay agent, mo 1 terminal khac va giu chromedriver chay nen:"
echo "  chromedriver --port=9515"