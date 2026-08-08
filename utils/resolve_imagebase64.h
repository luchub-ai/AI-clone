#pragma once
#include <string>
#include <optional>

// Khai báo hàm phân loại và xử lý chuỗi đầu vào (Path hay Base64?)
std::optional<std::string> resolveImageToBase64(const std::string& input_str);