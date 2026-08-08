#pragma once
#include <nlohmann/json.hpp>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>

// ════════════════════════════════════════════════════════════════
//  requireField<T>() — rút trích 1 field JSON kiểu T, trả nullopt nếu
//  field thiếu HOẶC sai kiểu.
//
//  TRƯỚC: gần như MỌI Tool (ExecTool, FileTool, CodeInterpreterTool,
//  MemoryTool...) đều tự viết lại y hệt:
//      if (!j.contains("x") || !j["x"].is_string()) return "Loi: ...";
//      std::string x = j["x"].get<std::string>();
//  lặp lại 2 dòng này cho từng field, từng Tool — dễ quên check kiểu
//  (memory_tool.cpp trước đây có chỗ gán thẳng `j["action"]` không qua
//  is_string(), chỉ ăn may vì json ném exception thay vì im lặng sai).
//
//  GIỜ: 1 lần gọi requireField<std::string>(j, "x") làm cả 2 việc, và
//  concept JsonScalar (C++20) CHẶN NGAY LÚC BIÊN DỊCH nếu ai đó lỡ gọi
//  với kiểu không có ánh xạ JSON rõ ràng (VD std::vector<Tool>) — lỗi
//  hiện ra ở chỗ gọi với thông báo dễ hiểu, thay vì lỗi mập mờ sâu bên
//  trong nlohmann::json.
// ════════════════════════════════════════════════════════════════
template <typename T>
concept JsonScalar =
    std::same_as<T, std::string> ||
    std::same_as<T, bool> ||
    std::integral<T> ||
    std::floating_point<T>;

template <JsonScalar T>
[[nodiscard]] std::optional<T> requireField(const nlohmann::json& j, std::string_view field) {
    // nlohmann::json::contains/operator[] chưa nhận std::string_view thẳng
    // ở phiên bản đang dùng trong repo -> ép về std::string 1 lần ở đây,
    // các chỗ gọi phía trên khỏi phải lặp lại.
    std::string key(field);
    if (!j.contains(key)) return std::nullopt;

    const auto& v = j[key];
    if constexpr (std::same_as<T, std::string>) {
        if (!v.is_string()) return std::nullopt;
    } else if constexpr (std::same_as<T, bool>) {
        if (!v.is_boolean()) return std::nullopt;
    } else if constexpr (std::integral<T>) {
        if (!v.is_number_integer()) return std::nullopt;
    } else if constexpr (std::floating_point<T>) {
        if (!v.is_number()) return std::nullopt;
    }
    return v.get<T>();
}
