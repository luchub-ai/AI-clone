# Setup WebBrowserTool (đọc trước khi build)

`WebBrowserTool` cần **chromedriver + Chrome for Testing** chạy sẵn trước khi
agent gọi tool `browser`. Thiếu 1 trong 2 hoặc thiếu đúng biến môi trường sẽ
gặp lỗi quen thuộc:

```
Loi tao session: ... session not created: cannot find Chrome binary
```

## Setup lần đầu (chỉ làm 1 lần / máy)

```bash
bash setup_chromedriver.sh
```

Script tự tải Chrome for Testing + chromedriver **khớp version** (không dùng
`apt install chromium` — dễ lệch version, chromedriver sẽ từ chối kết nối),
cài vào `~/chrome-for-testing/`, và cài các thư viện hệ thống cần thiết
(cần `sudo`).

## File `.env` (không commit — đã có trong `.gitignore`)

Copy vào repo, điền
key của riêng bạn:

```bash
TAVILY_API_KEY="xxxxxxxxxxxxxxxxxxxxxxxx"
# nhập key vô
BROWSER_BINARY_PATH="$HOME/chrome-for-testing/chrome-linux64/chrome"
CHROMEDRIVER_URL="http://127.0.0.1:9515"
OLLAMA_MODEL="gemma4:e4b"
OLLAMA_BASE_URL="http://localhost:11434"
```

`$HOME` tự động đúng theo máy mỗi người, không cần sửa nếu bạn không đổi
thư mục cài đặt của `setup_chromedriver.sh`.

## Chạy hằng ngày

```bash
chmod +x start_agent.sh   # chỉ cần 1 lần sau khi clone
./start_agent.sh
```

`start_agent.sh` tự lo:
1. `source .env` (không cần `export` tay).
2. Nếu máy chưa có chromedriver/Chrome for Testing → tự chạy
   `setup_chromedriver.sh` (sẽ hỏi mật khẩu sudo).
3. Nếu chưa build (`./build/run_eval` chưa tồn tại) → tự `cmake -B build -S .`
   + `cmake --build build`.
4. Bật `chromedriver --port=9515` nền nếu chưa chạy.
5. Chạy `./build/run_eval`, forward mọi tham số dòng lệnh — vd:
   ```bash
   ./start_agent.sh --tasks=benchmark/tasks.json --out=benchmark/results
   ```

## Nếu vẫn lỗi, kiểm tra nhanh theo thứ tự

```bash
curl -s http://127.0.0.1:9515/status                # chromedriver có sống không
echo $BROWSER_BINARY_PATH                            # có trỏ đúng file thật không
"$BROWSER_BINARY_PATH" --version                     # binary có chạy được không
```

Nếu cả 3 bước trên đều ổn mà vẫn lỗi, thử tạo session tay qua `curl` để cô
lập vấn đề C++ hay môi trường — hỏi Hiếu/Khánh nếu bí.

---
*Ghi chú tạm thời cho team, sẽ dọn lại thành README chính thức sau khi các
tool khác (calculator, file, code_interpreter...) cũng có hướng dẫn tương tự.*

ghi chus (ttự động update nếu ko cùng phiên)

sudo apt install --only-upgrade google-chrome-stable
