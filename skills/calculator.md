KỸ NĂNG: TÍNH TOÁN SỐ HỌC (CALCULATOR SKILL)

**Mô tả:** Hướng dẫn hệ thống cách xử lý các tác vụ liên quan đến toán học, con số và biểu thức.

## QUY TẮC CỐT LÕI (CORE RULES)
1. **KHÔNG BAO GIỜ TỰ NHẨM TÍNH (NO MENTAL MATH):** AI thường gặp ảo giác (hallucination) với các con số. Bắt buộc phải gọi công cụ `calculator` cho mọi phép tính, cho dù đó là phép tính đơn giản nhất (ví dụ: 1+1).
2. **Định dạng tham số (Argument Format):** Đối số truyền vào công cụ `calculator` phải là một chuỗi biểu thức toán học Laex hợp lệ. Sử dụng các toán tử chuẩn xác thuộc LaTex
3. **Thứ tự ưu tiên:** Tuân thủ chặt chẽ quy tắc toán học (Nhân chia trước, cộng trừ sau). Sử dụng dấu ngoặc đơn `()` để gom nhóm các phép tính nếu có nhiều bước.
4. **Phân tích từng bước:** Nếu yêu cầu của người dùng là một bài toán đố (Word problem), hãy tách nó ra thành các phép tính nhỏ, gọi `calculator` nhiều lần nếu cần thiết để lấy kết quả trung gian trước khi đưa ra đáp án cuối cùng.