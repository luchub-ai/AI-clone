#include <iostream>
#include <vector>
#include <memory>
#include <cassert>
#include "../src/client/ollama_client.h"
#include "../src/client/colab_client.h"

void printSeparator(const std::string& title) {
    std::cout << "\n========================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "========================================\n";
}

int main() {
    // -----------------------------------------------------------------
    // CẤU HÌNH THÔNG SỐ SERVER
    // -----------------------------------------------------------------
    std::string BASE_URL = "https://wondering-reel-companies-craft.trycloudflare.com"; // Thay bằng URL Colab nếu dùng Colab
    std::string MODEL    = "gemma4:e4b";                  // Thay bằng tên model bạn đang có
    float TEMP           = 0.7f;
    int MAX_TOKENS       = 150;

    std::cout << "[INIT] Khởi tạo OllamaClient qua con trỏ lớp cha (LLMClient*)...\n";
    
    // Áp dụng C++17 std::unique_ptr đúng yêu cầu bảng V đồ án
    std::unique_ptr<ColabClient> client = std::make_unique<ColabClient>(
        BASE_URL, MODEL, TEMP, MAX_TOKENS
    );

    std::cout << "[INFO] Client đang kết nối tới Model: " << client->getModelName() << "\n";

    // -----------------------------------------------------------------
    // KỊCH BẢN 1: TEST GỬI TIN NHẮN ĐƠN GIẢN
    // -----------------------------------------------------------------
    printSeparator("TEST 1: SINGLE TURN CHAT");
    {
        std::string prompt = "kiểm tra phép tính của hình ảnh trên là đúng hay sai, ghi ra nội dung của ảnh và nói lên nó sai điều gì";

        // [SỬA THEO PROTOTYPE MỚI]: Đóng gói prompt thành 1 tin nhắn User
        std::vector<Message> single_turn_history = {
            {"user", prompt, {"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"}}
        };

        std::cout << "User > " << prompt << "\n";
        std::cout << "AI   > Đang suy nghĩ...\n";

        // Chỉ truyền đúng 1 tham số history
        LLMResponse res = client->chat(single_turn_history);

        std::cout << "\n[KẾT QUẢ PHẢN HỒI]:\n";
        std::cout << " - Nội dung : " << res.content << "\n";
        std::cout << " - Tokens   : " << res.tokens_used << " tokens\n";
        std::cout << " - Độ trễ   : " << res.latency_ms << " ms\n";

        // Validate cơ bản
        if (!res.content.empty()) {
            std::cout << "=> [PASS] Test 1 thành công!\n";
        } else {
            std::cerr << "=> [FAILED] Test 1 thất bại (Content rỗng).\n";
        }
    }

    // -----------------------------------------------------------------
    // KỊCH BẢN 2: TEST KHẢ NĂNG NHỚ LỊCH SỬ HỘI THOẠI (MULTI-TURN)
    // -----------------------------------------------------------------
    printSeparator("TEST 2: CONVERSATION HISTORY");
    {
        std::string follow_up_prompt = "Tôi tên là gì và làm nghề gì?";

        // [SỬA THEO PROTOTYPE MỚI]: Nối tiếp câu hỏi mới vào sau các câu thoại cũ
        std::vector<Message> history = {
            {"user", "Tôi tên là Nam, một lập trình viên C++.", {}},
            {"assistant", "Chào Nam! Rất vui được gặp một đồng nghiệp C++. Tôi có thể giúp gì cho project của bạn?", {}},
            {"user", follow_up_prompt, {}} // Nhét luôn prompt mới vào cuối timeline
        };

        std::cout << "User > " << follow_up_prompt << "\n";
        std::cout << "AI   > Đang lục lại bộ nhớ...\n";

        // Chỉ truyền đúng 1 tham số history
        LLMResponse res = client->chat(history);

        std::cout << "\n[AI Phản hồi]: " << res.content << "\n";

        // Kiểm tra xem AI có nói ra được chữ "Nam" hoặc "C++" không
        bool remembered = (res.content.find("Nam") != std::string::npos) || 
                          (res.content.find("C++") != std::string::npos);

        if (remembered) {
            std::cout << "=> [PASS] Test 2: AI nhớ đúng ngữ cảnh!\n";
        } else {
            std::cout << "=> [WARNING] AI trả lời được nhưng có vẻ không nhớ chính xác tên.\n";
        }
    }

    // -----------------------------------------------------------------
    // KỊCH BẢN 3: TEST BẪY LỖI KHI URL SAI HỒNG/CHẾT SERVER
    // -----------------------------------------------------------------
    printSeparator("TEST 3: ERROR HANDLING (CONNECTION REFUSED)");
    {
        std::cout << "Cố tình kết nối tới một cổng không tồn tại (9999)...\n";
        auto dead_client = std::make_unique<OllamaClient>("http://localhost:9999", "ghost_model", 0.1f, 50);
        
        // [SỬA THEO PROTOTYPE MỚI]: Truyền vector chứa 1 message
        std::vector<Message> dead_history = { {"user", "Xin chào", {}} };
        LLMResponse res = dead_client->chat(dead_history);

        if (res.content.empty()) {
            std::cout << "=> [PASS] Test 3: Bẫy lỗi chuẩn! Trả về chuỗi rỗng một cách an toàn (Graceful failure).\n";
        } else {
            std::cerr << "=> [FAILED] Test 3 thất bại.\n";
        }
    }

    std::cout << "\n[HOÀN TẤT TOÀN BỘ BÀI TEST OLLAMA CLIENT]\n\n";
    return 0;
}