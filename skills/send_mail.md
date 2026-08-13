# SKILL: Gửi Email qua Giao diện (GUI)
Để gửi email, bạn BẮT BUỘC phải tuân thủ chính xác các bước sau. KHÔNG chuyển sang bước tiếp theo nếu ảnh chụp màn hình chưa xác nhận bước hiện tại thành công.

- Bước 1 [Mở App]: Dùng tool `exec` để mở trình duyệt vào trang mail.
- Bước 2 [XÁC THỰC MỞ APP]: Dừng lại và nhìn ảnh chụp màn hình. 
  -> Nếu thấy Terminal: App đang bị chìm. Chuyển sang "Quy trình Sửa lỗi (Fallback)".
  -> Nếu thấy Gmail: Chuyển sang Bước 3.
- Bước 3 [Soạn thảo]: Dùng `gui_input` click vào nút "Compose", gõ email người nhận, click vào tiêu đề, click vào nội dung.
- Bước 4 [Gửi]: Click nút "Send".

# Quy trình Sửa lỗi (Fallback) khi App bị chìm:
Nếu bạn thấy giao diện vẫn là Terminal sau khi mở app, hãy dùng tool `gui_input` với Action Input: {"action":"key","keys":"alt+tab"} để kéo cửa sổ app lên, HOẶC dùng `exec` với lệnh `wmctrl -a "Google Chrome"` để ép hệ điều hành đưa trình duyệt lên trên cùng.