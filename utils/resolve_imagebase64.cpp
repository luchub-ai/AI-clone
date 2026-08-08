// thằng này giúp hỗ trợ xác thực một chuỗi là imagebase64, hay đường dẫn của một ảnh

#include "utils/resolve_imagebase64.h"
#include "utils/encode_base64.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// Hàm kiểm tra xem chuỗi có kết thúc bằng đuôi ảnh quen thuộc không
bool hasImageExtension(const std::string& str) {
    auto dot_pos = str.find_last_of('.');
    if (dot_pos == std::string::npos) return false;
    
    std::string ext = str.substr(dot_pos);
    // Chuyển về chữ thường để check cho chuẩn
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".bmp");
}

std::optional<std::string> resolveImageToBase64(const std::string& input_str) {
    if (input_str.empty()) return std::nullopt;

    // LỚP 1: KIỂM TRA FILE AN TOÀN KHI CHẠY TRÊN HỆ ĐIỀU HÀNH
    // Chỉ check filesystem nếu độ dài hợp lý (< 4096)
    if (input_str.length() < 4096) {
        std::error_code ec; // Cực kỳ quan trọng: Nuốt trôi lỗi ENAMETOOLONG, không cho văng exception
        if (fs::exists(input_str, ec) && !ec && fs::is_regular_file(input_str, ec)) {
            std::cout << "[Sanitizer] Phát hiện file ảnh hợp lệ. Đang mã hóa: " << input_str << "...\n";
            return ImageUtils::imageToBase64String(input_str);
        }
    }

    // LỚP 2: PHÂN BIỆT RẠCH RÒI "BASE64" VÀ "ĐƯỜNG DẪN ẢO BỊ SAI"
    // Nếu chuỗi có chứa đuôi file (.png, .jpg...) nhưng lại lọt được xuống đây -> Chắc chắn 100% 
    // đây là một đường dẫn bị gõ sai hoặc file đã bị xóa, TUYỆT ĐỐI KHÔNG PHẢI Base64!
    if (hasImageExtension(input_str)) {
        std::cerr << "[Sanitizer] CẢNH BÁO: Đường dẫn ảnh không tồn tại trên ổ cứng: " 
                  << (input_str.length() > 60 ? input_str.substr(0, 60) + "..." : input_str) << "\n";
        return std::nullopt; // Tiêu hủy ngay lập tức!
    }

    // LỚP 3: XÁC NHẬN BASE64
    // Base64 thật thường rất dài và không bao giờ chứa đuôi file .png, .jpg
    if (input_str.length() > 100) {
        std::cout << "[Sanitizer] Nhận diện chuỗi Base64 hợp lệ (" << input_str.length() << " chars).\n";
        return input_str;
    }

    // Rơi vào đây là các chuỗi rác ngắn vô nghĩa (VD: "abc", "test", ...)
    std::cerr << "[Sanitizer] CẢNH BÁO: Bỏ qua chuỗi dữ liệu ảnh không hợp lệ.\n";
    return std::nullopt;
}