#include <iostream>
#include <memory>
#include <cstdlib>
#include <string>
#include <nlohmann/json.hpp>
#include "src/tools/web_search_tool.h" // Điều chỉnh lại đường dẫn include tùy cấu trúc thư mục của bạn

using json = nlohmann::json;

void printSeparator(const std::string& title) {
    std::cout << "\n==================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "==================================================\n";
}

int main() {
    printSeparator("KHỞI TẠO WEB SEARCH TOOL");

    // 1. Lấy API Key từ biến môi trường (TAVILY_API_KEY)
    const char* api_key_env = std::getenv("TAVILY_API_KEY");
    std::string api_key = api_key_env ? std::string(api_key_env) : "";

    if (api_key.empty()) {
        std::cerr << "[CẢNH BÁO] Chưa set biến môi trường TAVILY_API_KEY!\n";
        std::cerr << "Hãy chạy lệnh: export TAVILY_API_KEY=\"tvly-xxxx...\" trong Terminal trước khi build/test.\n";
        // Bạn có thể tạm paste hardcode key vào đây để test nhanh nếu muốn:
        // api_key = "tvly-dieu-khac-key-cua-ban-vao-day";
        return 1;
    }

    // 2. Khởi tạo tool với cấu hình chuẩn
    std::string tavily_url = "https://api.tavily.com";
    int max_results = 5;
    int timeout_sec = 15;

    auto search_tool = std::make_unique<WebSearchTool>(api_key, tavily_url, max_results, timeout_sec);
    std::cout << "[OK] Đã khởi tạo Tool: " << search_tool->getName() << "\n";

    // -----------------------------------------------------------------
    // TEST CASE: Tìm kiếm từ khóa "lê quang liêm là ai"
    // -----------------------------------------------------------------
    printSeparator("THỰC THI SEARCH QUERY");

    // 3. Đóng gói tham số đầu vào đúng chuẩn JSON mà description của Tool yêu cầu
    json args_json = {
        {"query", "lê quang liêm là ai"},
        {"num_results", 3},      // Chỉ lấy 3 kết quả tốt nhất để đọc cho nhanh
        {"time_range", "year"}   // Lấy thông tin trong 1 năm trở lại đây (tùy chọn)
    };

    std::string args_str = args_json.dump();
    std::cout << "[Input Args]: " << args_str << "\n\n";
    std::cout << "Đang gửi request lên Tavily API... (Vui lòng đợi vài giây)\n";

    // 4. Gọi thực thi hàm execute
    std::optional<std::string> result = search_tool->execute(args_str);

    // 5. Kiểm tra và in kết quả trả về
    printSeparator("KẾT QUẢ TRẢ VỀ TỪ TAVILY");
    if (result.has_value()) {
        std::cout << *result << "\n";
        std::cout << "=> [PASS] Test tìm kiếm thành công!\n";
    } else {
        std::cerr << "=> [FAILED] Tool trả về nullopt (Lỗi thực thi nghiêm trọng).\n";
    }

    printSeparator("HOÀN TẤT TEST CASE");
    return 0;
}