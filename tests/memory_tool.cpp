#include <iostream>
#include <cassert>
#include <string>
#include "src/tools/memory_tool.h"

// Hàm tiện ích để in kết quả test dễ nhìn hơn
void printTestResult(const std::string& test_name, const std::optional<std::string>& result) {
    std::cout << "--------------------------------------------------\n";
    std::cout << "[TEST] " << test_name << "\n";
    if (result.has_value()) {
        std::cout << ">> KẾT QUẢ: \n" << result.value() << "\n";
    } else {
        std::cout << ">> KẾT QUẢ: [Thất bại / nullopt]\n";
    }
    std::cout << "--------------------------------------------------\n\n";
}

int main() {
    std::cout << "BẮT ĐẦU TEST MEMORY TOOL...\n\n";

    // Khởi tạo tool với một file database dành riêng cho test
    MemoryTool memory_tool("test_agent_memory.db");

    // 1. Kiểm tra thông tin Tool
    std::cout << "Tên Tool: " << memory_tool.getName() << "\n";
    std::cout << "Mô tả Tool:\n" << memory_tool.getDescription() << "\n\n";

    // 2. Test LƯU thông tin hợp lệ
    std::string save_args_1 = R"({"action": "save", "data": "Mật khẩu nhà là 'Oophome2026!@#'"})";
    auto result_save_1 = memory_tool.execute(save_args_1);
    printTestResult("Lưu thông tin 1", result_save_1);

    std::string save_args_2 = R"({"action": "save", "data": "Tên của user là 'Nguyễn Văn A'"})";
    auto result_save_2 = memory_tool.execute(save_args_2);
    printTestResult("Lưu thông tin 2", result_save_2);

    // 3. Test TÌM KIẾM thông tin CÓ TỒN TẠI
    std::string search_args_1 = R"({"action": "search", "data": "Mật khẩu nhà, chìa khóa nhà, cửa sổ"})";
    auto result_search_1 = memory_tool.execute(search_args_1);
    printTestResult("Tìm kiếm thông tin (Có tồn tại)", result_search_1);

    // 4. Test TÌM KIẾM thông tin KHÔNG TỒN TẠI
    std::string search_args_2 = R"({"action": "search", "data": "Chìa khóa nhà"})";
    auto result_search_2 = memory_tool.execute(search_args_2);
    printTestResult("Tìm kiếm thông tin (Không tồn tại)", result_search_2);

    // 5. Test XỬ LÝ LỖI: JSON sai định dạng (mô phỏng LLM bị "ngáo")
    std::string bad_json_args = R"({action: "save", data: "Thiếu ngoặc kép ở key"})";
    auto result_bad_json = memory_tool.execute(bad_json_args);
    printTestResult("Xử lý lỗi JSON sai cú pháp", result_bad_json);

    // 6. Test XỬ LÝ LỖI: JSON thiếu trường bắt buộc
    std::string missing_field_args = R"({"action": "save"})"; // Thiếu "data"
    auto result_missing_field = memory_tool.execute(missing_field_args);
    printTestResult("Xử lý lỗi thiếu trường data", result_missing_field);

    // 7. Test XỬ LÝ LỖI: Hành động (action) không hợp lệ
    std::string invalid_action_args = R"({"action": "delete", "data": "Xóa ký ức"})";
    auto result_invalid_action = memory_tool.execute(invalid_action_args);
    printTestResult("Xử lý lỗi Action không hợp lệ", result_invalid_action);

    std::cout << "HOÀN THÀNH TEST!\n";
    return 0;
}