KỸ NĂNG: CHẠY CODE THỰC THI (CODE INTERPRETER SKILL)

**Mô tả:** Hướng dẫn hệ thống khi nào nên dùng `code_interpreter` thay vì `calculator` hoặc `exec`, và cách gọi cho đúng.

## QUY TẮC CỐT LÕI (CORE RULES)
1. **DÙNG KHI CẦN LOGIC NHIỀU BƯỚC:** Nếu bài toán cần vòng lặp, điều kiện, đệ quy, hoặc xử lý chuỗi/danh sách mà `calculator` (chỉ nhận 1 biểu thức) không đủ, hãy dùng `code_interpreter`. Nếu chỉ là 1 phép tính số học đơn giản, vẫn ưu tiên `calculator`.
2. **STATELESS - LUÔN TỰ KHAI BÁO LẠI:** Mỗi lần gọi `code_interpreter` là MỘT chương trình độc lập hoàn toàn, không nhớ biến/import của lần gọi trước. Phải viết lại từ đầu (kể cả các dòng `import`) trong mỗi lần gọi.
3. **ĐỊNH DẠNG THAM SỐ:** Đối số truyền vào PHẢI là JSON hợp lệ dạng `{"code": "..."}`, với `code` là một chuỗi chứa toàn bộ đoạn code cần chạy (dùng `\n` để xuống dòng bên trong chuỗi).
4. **IN KẾT QUẢ RA STDOUT:** Công cụ chỉ trả về những gì được in ra - PHẢI dùng `print(...)` cho bất kỳ giá trị nào cần xem, code không tự hiển thị giá trị biểu thức cuối như một REPL.
5. **XỬ LÝ LỖI:** Nếu Observation trả về là lỗi JSON hoặc traceback, đọc kỹ thông báo, sửa lại `code` cho đúng rồi gọi lại - không lặp lại y hệt lỗi cũ ở lượt sau.
