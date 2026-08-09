KỸ NĂNG: TRÍ NHỚ DÀI HẠN (MEMORY SKILL)

**Mô tả:** Hướng dẫn hệ thống khi nào và cách gọi `memory_tool` để lưu và tìm lại thông tin.

## QUY TẮC CỐT LÕI (CORE RULES)
1. **LƯU KHI GẶP THÔNG TIN ĐÁNG NHỚ:** Thấy thông tin có giá trị lâu dài (sở thích, dữ liệu cá nhân, quyết định, sự kiện...) thì gọi `{"action": "save", "data": "<nội dung>"}`. Viết `data` thành câu đầy đủ ngữ cảnh, đọc lại hiểu ngay không cần đoán.
2. **TÌM BẰNG CÂU MÔ TẢ TỰ NHIÊN:** Gọi `{"action": "search", "data": "<mô tả điều đang tìm>"}` với `data` là 1 câu mô tả ngắn gọn (VD: "sở thích ăn uống của user"). Công cụ tự tìm theo ngữ nghĩa gần đúng - KHÔNG bắt buộc liệt kê nhiều từ khóa rời rạc như trước.
3. **VÀI TỪ KHÓA VẪN DÙNG ĐƯỢC:** Nếu không chắc nên mô tả sao, truyền vài từ liên quan cách nhau bởi dấu phẩy cũng chấp nhận được - công cụ vẫn hiểu và có cơ chế dự phòng dựa trên từ khóa khi cần.
4. **KHÔNG TỰ BỊA KẾT QUẢ:** Observation báo không tìm thấy thì thừa nhận là chưa có thông tin đó trong bộ nhớ, không suy diễn hay bịa nội dung.
