#include "memory_tool.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <filesystem>
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

// ---------------------------------------------------------------------
// Serialize/deserialize vector<float> <-> BLOB thô (không header, không
// kiểm tra endianness - đồ án chỉ chạy trên 1 máy/kiến trúc lúc demo,
// không cần portable cross-machine). Đánh dấu `static` (internal linkage)
// vì đây là helper riêng của file này - tránh đụng độ tên với hàm ở file
// .cpp khác trong project (GLOB_RECURSE gom hết vào chung 1 executable).
// ---------------------------------------------------------------------
static std::vector<uint8_t> serializeEmbedding(const std::vector<float>& embedding) {
    std::vector<uint8_t> blob(embedding.size() * sizeof(float));
    if (!embedding.empty()) {
        // memcpy thay vì reinterpret_cast con trỏ để tránh vi phạm strict
        // aliasing (UB ở -O2/-O3 dù "chạy đúng" ở -O0).
        std::memcpy(blob.data(), embedding.data(), blob.size());
    }
    return blob;
}

static std::vector<float> deserializeEmbedding(const void* blob_data, int blob_size) {
    std::vector<float> embedding;
    if (!blob_data || blob_size <= 0 || blob_size % static_cast<int>(sizeof(float)) != 0) {
        return embedding; // rỗng -> nơi gọi phải tự kiểm tra .empty() trước khi dùng
    }
    embedding.resize(static_cast<size_t>(blob_size) / sizeof(float));
    std::memcpy(embedding.data(), blob_data, static_cast<size_t>(blob_size));
    return embedding;
}

float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) {
        return 0.0f;
    }

    // Cộng dồn bằng double dù input là float: 768 chiều (nomic-embed-text)
    // cộng dồn qua nhiều số hạng bằng float thuần dễ tích lũy sai số làm
    // tròn hơn double, dùng double cho ổn định số học rồi ép về float lúc
    // trả kết quả.
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot    += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        norm_a += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        norm_b += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }

    if (norm_a == 0.0 || norm_b == 0.0) {
        return 0.0f; // Tránh chia cho 0 / NaN nếu 1 trong 2 là vector-không
    }

    return static_cast<float>(dot / (std::sqrt(norm_a) * std::sqrt(norm_b)));
}

MemoryTool::MemoryTool(const std::string& db_path, std::shared_ptr<EmbeddingClient> embedder)
    : db_(nullptr), db_path_(db_path), embedder_(std::move(embedder)) {

    // Tự tạo thư mục cha (vd "data/") nếu chưa tồn tại - sqlite3_open()
    // KHÔNG tự làm việc này, sẽ lỗi CANTOPEN nếu thư mục chưa có.
    std::filesystem::path p(db_path_);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            std::cerr << "Cảnh báo: không tạo được thư mục '" << p.parent_path()
                       << "': " << ec.message() << "\n";
        }
    }

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
    // DB mới tinh -> tạo thẳng với cột "embedding" (BLOB, cho phép NULL
    // khi chưa/không thể embed). DB cũ đã tồn tại bảng "memories" từ
    // trước feature này -> CREATE TABLE IF NOT EXISTS sẽ không làm gì (nó
    // KHÔNG tự thêm cột vào bảng đã tồn tại), nên cần bước migrate PRAGMA
    // bên dưới để xử lý riêng trường hợp đó.
    const char* create_sql = R"(
        CREATE TABLE IF NOT EXISTS memories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            content TEXT NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            embedding BLOB
        );
    )";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, create_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Lỗi tạo bảng: " << err_msg << "\n";
        sqlite3_free(err_msg);
        return false;
    }

    // --- Migration cho DB cũ (tạo trước khi có feature vector search) ---
    // PRAGMA table_info liệt kê từng cột hiện có trong bảng "memories".
    // Kiểm tra xem cột "embedding" đã tồn tại chưa TRƯỚC khi ALTER TABLE,
    // tránh lỗi "duplicate column name" nếu MemoryTool được khởi tạo lại
    // nhiều lần trên cùng 1 DB đã được migrate từ trước đó.
    bool has_embedding_column = false;
    const char* pragma_sql = "PRAGMA table_info(memories);";
    sqlite3_stmt* pragma_stmt;
    if (sqlite3_prepare_v2(db_, pragma_sql, -1, &pragma_stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Lỗi đọc PRAGMA table_info(memories).\n";
        return false;
    }
    while (sqlite3_step(pragma_stmt) == SQLITE_ROW) {
        // Cột thứ 2 (index 1) của PRAGMA table_info là tên cột ("name").
        const unsigned char* col_name = sqlite3_column_text(pragma_stmt, 1);
        if (col_name && std::string(reinterpret_cast<const char*>(col_name)) == "embedding") {
            has_embedding_column = true;
            break;
        }
    }
    sqlite3_finalize(pragma_stmt);

    if (!has_embedding_column) {
        const char* alter_sql = "ALTER TABLE memories ADD COLUMN embedding BLOB;";
        if (sqlite3_exec(db_, alter_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "Lỗi migrate (thêm cột embedding): " << err_msg << "\n";
            sqlite3_free(err_msg);
            return false;
        }
        std::cout << "[MemoryTool] Đã migrate DB cũ tại '" << db_path_
                   << "': thêm cột 'embedding' (BLOB). Dữ liệu cũ được giữ nguyên.\n";
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
    const char* sql = "INSERT INTO memories (content, embedding) VALUES (?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt; // Truy vấn thất bại
    }

    // Bind giá trị data vào câu query để tránh SQL Injection
    sqlite3_bind_text(stmt, 1, data.c_str(), -1, SQLITE_TRANSIENT);

    // Chỉ gọi embed() khi có embedder_ được cấu hình (constructor mặc định
    // là nullptr). embed() có thể trả về nullopt (Ollama tắt/timeout giữa
    // demo) - vẫn lưu được nội dung text bình thường, chỉ mất khả năng tìm
    // theo ngữ nghĩa riêng cho dòng này (cột embedding = NULL), KHÔNG làm
    // crash chương trình hay mất dữ liệu.
    std::optional<std::vector<float>> embedding_opt;
    if (embedder_) {
        embedding_opt = embedder_->embed(data);
    }

    std::vector<uint8_t> blob;
    if (embedding_opt.has_value()) {
        blob = serializeEmbedding(*embedding_opt);
        sqlite3_bind_blob(stmt, 2, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    sqlite3_finalize(stmt);

    if (embedder_ && !embedding_opt.has_value()) {
        return "Đã lưu thông tin vào bộ nhớ thành công (không tạo được embedding - đã lưu dạng text thường).";
    }
    return "Đã lưu thông tin vào bộ nhớ thành công.";
}

std::optional<std::string> MemoryTool::searchMemory(const std::string& data) {
    if (trim(data).empty()) {
        return "Lỗi: Nội dung tìm kiếm rỗng.";
    }

    if (embedder_) {
        auto query_embedding = embedder_->embed(data);
        if (query_embedding.has_value()) {
            return searchMemoryByVector(data, *query_embedding);
        }
        // embed() thất bại giữa chừng (vd Ollama crash lúc demo) -> fallback
        // về LIKE keyword thay vì trả lỗi thất bại hoàn toàn cho agent.
        std::cerr << "[MemoryTool] Cảnh báo: embed() thất bại khi search, fallback về LIKE keyword.\n";
    }

    return searchMemoryByKeyword(data);
}

std::optional<std::string> MemoryTool::searchMemoryByKeyword(const std::string& data) {
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

std::optional<std::string> MemoryTool::searchMemoryByVector(const std::string& query,
                                                              const std::vector<float>& query_embedding) {
    const char* sql = "SELECT timestamp, content, embedding FROM memories;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt; // Lỗi CSDL
    }

    struct ScoredRow {
        float score;
        std::string timestamp;
        std::string content;
    };
    std::vector<ScoredRow> scored;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // Dòng có embedding = NULL (DB cũ trước migration, hoặc được lưu
        // lúc Ollama tắt) -> sqlite3_column_blob trả về nullptr/size 0.
        // BẮT BUỘC bỏ qua dòng này khi tính cosine thay vì đưa thẳng vào
        // cosineSimilarity() - deserializeEmbedding() đã tự trả vector rỗng
        // trong trường hợp này nhưng vẫn kiểm tra tường minh ở đây cho rõ
        // ràng, tránh 1 "điểm 0.0 tuyệt đối" làm sai thứ tự rank (0.0 là
        // điểm hợp lệ cho vector vuông góc, khác với "không có embedding").
        const void* blob_data = sqlite3_column_blob(stmt, 2);
        int blob_size = sqlite3_column_bytes(stmt, 2);
        if (!blob_data || blob_size <= 0) {
            continue;
        }

        std::vector<float> row_embedding = deserializeEmbedding(blob_data, blob_size);
        if (row_embedding.empty()) {
            continue;
        }

        float score = cosineSimilarity(query_embedding, row_embedding);

        const unsigned char* time_text = sqlite3_column_text(stmt, 0);
        const unsigned char* content_text = sqlite3_column_text(stmt, 1);

        scored.push_back(ScoredRow{
            score,
            time_text ? reinterpret_cast<const char*>(time_text) : "",
            content_text ? reinterpret_cast<const char*>(content_text) : ""
        });
    }
    sqlite3_finalize(stmt);

    if (scored.empty()) {
        return "Không có ký ức nào có embedding để tìm theo ngữ nghĩa (bộ nhớ đang trống, hoặc toàn bộ đều được lưu lúc chưa có embedding).";
    }

    std::sort(scored.begin(), scored.end(),
              [](const ScoredRow& lhs, const ScoredRow& rhs) { return lhs.score > rhs.score; });

    // Đề bài không yêu cầu con số top-K cụ thể - chọn 5 cho gọn và đủ dùng
    // (dễ sửa nếu cần), cùng tinh thần "đừng over-engineer" như phần ANN
    // index đã được đề nói rõ là không cần.
    constexpr size_t kTopK = 5;

    std::stringstream final_result;
    final_result << "Kết quả tìm kiếm (semantic) cho truy vấn: \"" << query << "\"\n";

    size_t shown = 0;
    for (const auto& row : scored) {
        if (shown >= kTopK) break;
        final_result << "  [similarity: " << std::fixed << std::setprecision(3) << row.score
                     << "] [" << row.timestamp << "] " << row.content << "\n";
        ++shown;
    }

    return final_result.str();
}
