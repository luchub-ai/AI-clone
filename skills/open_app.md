# TÊN KỸ NĂNG: Mở Ứng Dụng và Xác Thực Giao Diện (Open App & Verify)
# MÔ TẢ: BẮT BUỘC sử dụng kỹ năng này mỗi khi người dùng yêu cầu mở trình duyệt web (Chrome, Gmail, Google...) hoặc bất kỳ ứng dụng đồ họa nào.

Để mở ứng dụng thành công, bạn PHẢI thực hiện tuần tự các bước sau. TUYỆT ĐỐI KHÔNG được chuyển bước nếu chưa nhìn thấy sự thay đổi trên ảnh chụp màn hình (Screenshot).

## BƯỚC 1: Gọi lệnh mở ứng dụng (Execute) 
- Dùng công cụ `exec` để gọi lệnh mở app.
- LƯU Ý QUAN TRỌNG VỚI CHROME: Để tránh bị kẹt ở bảng chọn hồ sơ (Profile Picker) và ép mở cửa sổ mới, LUÔN LUÔN sử dụng cờ `--profile-directory="Default"` và `--new-window`.
- Ví dụ mở Gmail: 
  Action Input: {"command": "google-chrome --profile-directory=\"Default\" --new-window https://mail.google.com"}

## BƯỚC 2: Kiểm chứng thị giác (Visual Verification) - BƯỚC SỐNG CÒN
- Sau khi lệnh `exec` chạy xong, hệ thống sẽ trả về chữ "Opening in existing browser...". BẠN KHÔNG ĐƯỢC TIN VÀO DÒNG CHỮ NÀY.
- Bạn BẮT BUỘC phải phân tích (Screen Analysis) bức ảnh chụp màn hình MỚI NHẤT.
- CÂU HỎI KIỂM TRA: Trên màn hình hiện tại đang hiển thị giao diện của Trình duyệt Web (Chrome) hay vẫn đang là màn hình Terminal đen thui?
  -> NẾU LÀ TRÌNH DUYỆT (Thành công): Chuyển sang thao tác tiếp theo của bạn (ví dụ: dùng gui_input để click, type).
  -> NẾU VẪN LÀ TERMINAL (Thất bại / App mở chìm): Chuyển ngay sang BƯỚC 3.

## BƯỚC 3: Quy trình Sửa lỗi (Fallback) khi App bị chìm LƯU Ý QUAN TRỌNG : KHI MÀ BẠN LÀM XONG 2 BƯỚC TRÊN THÌ MỚI ĐƯỢC LÀM CÁI BƯỚC NÀY
Nếu ảnh chụp màn hình vẫn là Terminal, có nghĩa là Chrome đã mở nhưng bị hệ điều hành giấu xuống dưới. Hãy làm 1 trong 2 cách sau để lôi nó lên:
- Cách 1 (Dùng Phím tắt): Dùng công cụ `gui_input` để giả lập nhấn phím Alt + Tab.
  Action Input: {"action": "key", "keys": "alt+tab"}
- Cách 2 (Dùng wmctrl): Dùng công cụ `exec` để ép hệ điều hành focus vào cửa sổ.
  Action Input: {"command": "wmctrl -a \"Chrome\""}

YÊU CẦU CUỐI CÙNG: Không bao giờ được dùng `gui_input` để click/type nếu bạn chưa nhìn thấy rõ ràng các nút bấm/ô nhập liệu của ứng dụng trên ảnh chụp màn hình.