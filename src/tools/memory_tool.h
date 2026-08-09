#pragma once
#include "tool.h"
#include <string>
#include <optional>
#include <sqlite3.h> // Yêu cầu thư viện SQLite3 C API

class MemoryTool : public Tool {
private:
    sqlite3* db_;
    std::string db_path_;

    // Khởi tạo bảng nếu chưa có
    bool initializeDatabase();

    // Các hàm xử lý nội bộ
    std::optional<std::string> saveMemory(const std::string& data);
    std::optional<std::string> searchMemory(const std::string& keyword);

public:
    // C++17: Constructor explicit chống convert ngầm định
    explicit MemoryTool(const std::string& db_path = "data/agent_memory.db"); // xem nó nên được trỏ vô đâu
    
    ~MemoryTool() override;

    // Ghi đè (override) các hàm từ interface
    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;
};