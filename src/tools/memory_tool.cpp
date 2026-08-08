#include "memory_tool.h"
#include <iostream>
#include <sstream>
// Thư viện xử lý JSON yêu cầu trong mục II. Mục tiêu học tập
#include <nlohmann/json.hpp> 

using json = nlohmann::json;


// Hàm xóa khoảng trắng ở đầu và cuối chuỗi
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Hàm tách chuỗi bằng dấu phẩy
std::vector<std::string> splitKeywords(const std::string& data) {
    std::vector<std::string> result;
    std::stringstream ss(data);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string trimmed = trim(item);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

MemoryTool::MemoryTool(const std::string& db_path) : db_path_(db_path), db_(nullptr) {
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "Lỗi: Không thể mở database SQLite tại " << db_path_ << "\n";
    } else {
        initializeDatabase();
    }
}

MemoryTool::~MemoryTool() {
    if (db_) {
        sqlite3_close(db_);
    }
}

bool MemoryTool::initializeDatabase() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS memories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            content TEXT NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Lỗi tạo bảng: " << err_msg << "\n";
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

std::string MemoryTool::getName() const {
    return "memory_tool";
}

std::string MemoryTool::getDescription() const {
    return R"(Công cụ lưu trữ và truy vấn trí nhớ dài hạn. 
Tham số đầu vào (args) phải là một chuỗi JSON hợp lệ có định dạng:
{"action": "save", "data": "nội dung cần nhớ"} HOẶC 
{"action": "search", "data": "từ khóa tìm kiếm"})";
}

std::optional<std::string> MemoryTool::execute(const std::string& args) {
    if (!db_) {
        return "Lỗi nội bộ: Database chưa được khởi tạo.";
    }

    try {
        // Phân tích chuỗi JSON do LLM sinh ra
        json j = json::parse(args);
        
        if (!j.contains("action") || !j.contains("data")) {
            return "Lỗi: Tham số JSON thiếu 'action' hoặc 'data'.";
        }

        // [SỬA - Bug A]: truoc gan thang `std::string action = j["action"];`
        // -> neu LLM sinh "action"/"data" khong phai string (so, null, bool,
        // mang...) thi phep gan nay nem json::type_error, ma catch ben duoi
        // CHI bat json::parse_error -> exception thoat thang len
        // HarnessRunner, lam ca task bi tinh la "agent_crashed" thay vi chi
        // tra 1 dong loi cho agent thu lai. Kiem tra kieu tuong minh truoc
        // khi gan de tranh throw, tra loi ro rang cho LLM tu sua.
        if (!j["action"].is_string() || !j["data"].is_string()) {
            return "Lỗi: 'action' và 'data' phải là kiểu string.";
        }

        std::string action = j["action"];
        std::string data = j["data"];

        if (action == "save") {
            return saveMemory(data);
        } else if (action == "search") {
            return searchMemory(data);   
        } else {
            return "Lỗi: 'action' chỉ được là 'save' hoặc 'search'.";
        }

    } catch (const json::exception& e) {
        // [SỬA - Bug A]: bat rong json::exception (lop co so cua ca
        // parse_error lan type_error/out_of_range...) thay vi chi
        // parse_error, phong truong hop con sot loi kieu du lieu nao khac
        // ma kiem tra tuong minh o tren chua luong het.
        return std::string("Lỗi xử lý JSON: ") + e.what();
    }
}

std::optional<std::string> MemoryTool::saveMemory(const std::string& data) {
    const char* sql = "INSERT INTO memories (content) VALUES (?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt; // Truy vấn thất bại
    }

    // Bind giá trị data vào câu query để tránh SQL Injection
    sqlite3_bind_text(stmt, 1, data.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    sqlite3_finalize(stmt);
    return "Đã lưu thông tin vào bộ nhớ thành công.";
}

std::optional<std::string> MemoryTool::searchMemory(const std::string& data) {
    // 1. Tách chuỗi data thành danh sách từ khóa
    std::vector<std::string> keywords = splitKeywords(data);
    if (keywords.empty()) {
        return "Lỗi: Không có từ khóa nào hợp lệ được cung cấp.";
    }

    std::stringstream final_result;
    final_result << "Kết quả tìm kiếm cho các từ khóa:\n";

    // Chuẩn bị câu lệnh SQL (chỉ cần prepare 1 lần để tối ưu hiệu suất)
    const char* sql = "SELECT timestamp, content FROM memories WHERE content LIKE ? ORDER BY timestamp DESC;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt; // Lỗi CSDL
    }

    // 2. Lặp qua từng từ khóa và truy vấn độc lập
    for (const auto& keyword : keywords) {
        final_result << "--- Từ khóa: [" << keyword << "] ---\n";
        
        std::string like_query = "%" + keyword + "%";
        
        // Bind từ khóa hiện tại vào câu lệnh SQL
        sqlite3_bind_text(stmt, 1, like_query.c_str(), -1, SQLITE_TRANSIENT);

        bool found = false;
        
        // Đọc tất cả các dòng kết quả cho từ khóa này
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            found = true;
            const unsigned char* time_text = sqlite3_column_text(stmt, 0);
            const unsigned char* content_text = sqlite3_column_text(stmt, 1);
            
            final_result << "  [" << time_text << "] " << content_text << "\n";
        }

        // Nếu không có dòng nào được trả về
        if (!found) {
            final_result << "  -> Không tìm thấy ký ức nào chứa từ khóa này.\n";
        }
        final_result << "\n";

        // QUAN TRỌNG: Reset statement để dùng lại cho từ khóa tiếp theo trong vòng lặp
        sqlite3_reset(stmt);
    }

    // Dọn dẹp bộ nhớ
    sqlite3_finalize(stmt);

    return final_result.str();
}