// hàm này mã hóa từ link về base64

#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <optional>
#include <filesystem>

namespace ImageUtils {

// 1. Hàm hỗ trợ: Mã hóa mảng nhị phân sang chuỗi Base64
inline std::string encodeBase64(const std::vector<unsigned char>& data) {
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string encoded_string;
    int i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (unsigned char c : data) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; i < 4; i++) encoded_string += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++) encoded_string += base64_chars[char_array_4[j]];
        while((i++ < 3)) encoded_string += '=';
    }

    return encoded_string;
}

// 2. Hàm chính: Nhận đường dẫn ảnh -> Trả về CHUỖI Base64
inline std::optional<std::string> imageToBase64String(const std::string& imagePath) {
    // Mở file ở chế độ nhị phân (binary) và đặt con trỏ ở cuối file (ate) để lấy kích thước
    std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[ImageUtils] Lỗi: Không thể mở file ảnh tại " << imagePath << "\n";
        return std::nullopt;
    }

    // Lấy kích thước và đọc toàn bộ file vào buffer
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "[ImageUtils] Lỗi: Không đọc được dữ liệu từ file " << imagePath << "\n";
        return std::nullopt;
    }

    return encodeBase64(buffer);
}

// 3. Hàm nâng cao: Nhận đường dẫn ảnh -> Tạo file .txt chứa Base64 -> Trả về ĐƯỜNG DẪN FILE ĐÓ
inline std::optional<std::string> imageToBase64File(const std::string& imagePath, const std::string& outputDir = ".") {
    auto base64_str = imageToBase64String(imagePath);
    if (!base64_str.has_value()) return std::nullopt;

    try {
        std::filesystem::create_directories(outputDir);
        
        // Tạo tên file đầu ra dựa trên tên ảnh gốc (VD: test.png -> test_base64.txt)
        std::filesystem::path p(imagePath);
        std::string out_filename = p.stem().string() + "_base64.txt";
        std::filesystem::path out_path = std::filesystem::path(outputDir) / out_filename;

        std::ofstream out_file(out_path);
        if (!out_file.is_open()) return std::nullopt;

        out_file << *base64_str;
        out_file.close();

        return out_path.string(); // Trả về đường dẫn của file txt vừa tạo
    } catch (const std::exception& e) {
        std::cerr << "[ImageUtils] Lỗi khi ghi file base64: " << e.what() << "\n";
        return std::nullopt;
    }
}

} // namespace ImageUtils