#!/usr/bin/env bash
# setup_ydotool.sh
# Cai dat + cau hinh ydotool cho InputTool (bonus GUI agent).
# Chay: chmod +x setup_ydotool.sh && ./setup_ydotool.sh
# (Khong can chay bang sudo - script tu goi sudo o nhung cho can.)
set -euo pipefail

echo "== ydotool setup =="
echo ""

# ---- 1. Cai dat ydotool ----
if ! command -v ydotool >/dev/null 2>&1; then
    echo "[1/6] ydotool chua co, dang cai dat..."
    if command -v pacman >/dev/null 2>&1; then
        sudo pacman -Sy --needed --noconfirm ydotool
    elif command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y ydotool
    else
        echo "Khong nhan dien duoc package manager (khong phai pacman/apt)." >&2
        echo "Tu cai ydotool thu cong roi chay lai script nay." >&2
        exit 1
    fi
else
    echo "[1/6] ydotool da co san, bo qua."
fi

# ---- 2. Them user vao group input ----
NEED_RELOGIN=0
if ! groups "$USER" | grep -qw input; then
    echo "[2/6] Them $USER vao group 'input'..."
    sudo usermod -aG input "$USER"
    NEED_RELOGIN=1
else
    echo "[2/6] $USER da o trong group 'input'."
fi

# ---- 3. udev rule cho /dev/uinput ----
UDEV_RULE="/etc/udev/rules.d/99-uinput.rules"
UDEV_CONTENT='KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"'
if [ ! -f "$UDEV_RULE" ] || ! grep -qF "$UDEV_CONTENT" "$UDEV_RULE"; then
    echo "[3/6] Tao udev rule cho /dev/uinput..."
    echo "$UDEV_CONTENT" | sudo tee "$UDEV_RULE" > /dev/null
    sudo udevadm control --reload-rules
    sudo udevadm trigger
else
    echo "[3/6] udev rule da ton tai, bo qua."
fi

# ---- 4. systemd --user service ----
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_FILE="$SERVICE_DIR/ydotoold.service"
mkdir -p "$SERVICE_DIR"
cat > "$SERVICE_FILE" <<'EOF'
[Unit]
Description=ydotool daemon

[Service]
ExecStart=/usr/bin/ydotoold --socket-path=%h/.ydotool_socket --socket-own=%U:%G
Restart=on-failure

[Install]
WantedBy=default.target
EOF
echo "[4/6] Da ghi $SERVICE_FILE"

systemctl --user daemon-reload
systemctl --user enable ydotoold >/dev/null 2>&1 || true
systemctl --user reset-failed ydotoold >/dev/null 2>&1 || true

# ---- 5. Bien moi truong YDOTOOL_SOCKET ----
FISH_CONFIG="$HOME/.config/fish/config.fish"
mkdir -p "$(dirname "$FISH_CONFIG")"
touch "$FISH_CONFIG"
if ! grep -qF "YDOTOOL_SOCKET" "$FISH_CONFIG"; then
    echo 'set -gx YDOTOOL_SOCKET $HOME/.ydotool_socket' >> "$FISH_CONFIG"
    echo "[5/6] Da them YDOTOOL_SOCKET vao $FISH_CONFIG"
else
    echo "[5/6] YDOTOOL_SOCKET da co trong $FISH_CONFIG, bo qua."
fi

# Them ca cho bash (phong khi build/CI chay bash), khong anh huong shell chinh.
BASH_RC="$HOME/.bashrc"
if [ -f "$BASH_RC" ] && ! grep -qF "YDOTOOL_SOCKET" "$BASH_RC"; then
    echo 'export YDOTOOL_SOCKET=$HOME/.ydotool_socket' >> "$BASH_RC"
fi

# ---- 6. Khoi dong lai va kiem tra ----
echo "[6/6] Khoi dong lai ydotoold..."
RESTART_OK=1
systemctl --user restart ydotoold || RESTART_OK=0
sleep 1

echo ""
if [ "$RESTART_OK" = "1" ] && systemctl --user is-active --quiet ydotoold; then
    echo "OK: ydotoold dang chay (active)."
else
    echo "CANH BAO: ydotoold chua active. Log gan nhat:"
    systemctl --user status ydotoold --no-pager || true
fi

if [ "$NEED_RELOGIN" = "1" ]; then
    echo ""
    echo "!!! QUAN TRONG !!!"
    echo "Ban vua duoc them vao group 'input'. Group nay CHI co hieu luc"
    echo "sau khi dang xuat/dang nhap lai (hoac: sudo reboot)."
    echo "Neu buoc [6/6] o tren van bao loi Permission denied, hay:"
    echo "  1) Dang xuat roi dang nhap lai (hoac reboot)"
    echo "  2) Mo terminal moi, chay lai: systemctl --user restart ydotoold"
fi

echo ""
echo "Test nhanh (mo terminal moi de nhan YDOTOOL_SOCKET, hoac chay lenh set -gx duoi day):"
echo '  set -gx YDOTOOL_SOCKET $HOME/.ydotool_socket'
echo "  ydotool mousemove --absolute -x 100 -y 100"