#pragma once
#include <fstream>
#include <string>
#include <cstdlib>
#include <iostream>

namespace EnvUtils {
    inline void loadEnv(const std::string& filepath = ".env") {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cout << "[EnvLoader] Không tìm thấy file " << filepath << ", bỏ qua.\n";
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            // Bỏ qua dòng trống hoặc dòng ghi chú (bắt đầu bằng #)
            if (line.empty() || line[0] == '#') continue;
            
            auto pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                // Xóa dấu ngoặc kép "" nếu có (VD: "tvly-xxx" -> tvly-xxx)
                if (!value.empty() && (value.front() == '"' || value.front() == '\'')) value.erase(0, 1);
                if (!value.empty() && (value.back() == '"' || value.back() == '\'')) value.pop_back();
                
                // Nạp thẳng vào biến môi trường của tiến trình C++ (Linux setenv)
                // Thay thế dòng: setenv(key.c_str(), value.c_str(), 1);
                // Bằng đoạn code đa nền tảng sau:

                #ifdef _WIN32
                    _putenv_s(key.c_str(), value.c_str()); // Hàm dành cho Windows
                #else
                    setenv(key.c_str(), value.c_str(), 1); // Hàm POSIX dành cho Linux / macOS
                #endif
            }
        }
        std::cout << "[EnvLoader] Đã nạp thành công các biến từ file " << filepath << "!\n";
    }
}