#pragma once
#include <string>
#include <optional>

class Tool {
public:
    // BẮT BUỘC phải có virtual destructor để std::unique_ptr<Tool> không bị leak memory!
    virtual ~Tool() = default;

    [[nodiscard]] virtual std::string getName() const = 0;
    [[nodiscard]] virtual std::string getDescription() const = 0;

    // Trả về std::optional vì tool có thể chạy thất bại (ví dụ: file không tồn tại)
    virtual std::optional<std::string> execute(const std::string& args) = 0;
};