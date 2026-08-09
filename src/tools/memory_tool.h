#pragma once
#include "tool.h"
#include "src/client/embedding_client.h"
#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <sqlite3.h> // Yêu cầu thư viện SQLite3 C API

class MemoryTool : public Tool {
private:
    sqlite3* db_;
    std::string db_path_;
    std::shared_ptr<EmbeddingClient> embedder_;

    // Khởi tạo bảng nếu chưa có + migrate DB cũ (thêm cột "embedding"
    // qua PRAGMA table_info nếu thiếu, không đụng dữ liệu cũ)
    bool initializeDatabase();

    // Các hàm xử lý nội bộ
    std::optional<std::string> saveMemory(const std::string& data);

    // Dispatcher: co embedder_ va embed() thanh cong -> searchMemoryByVector.
    // Nguoc lai (khong cau hinh embedder_, HOAC embed() tra nullopt giua
    // chung vi Ollama sap) -> fallback ve searchMemoryByKeyword.
    std::optional<std::string> searchMemory(const std::string& data);

    // Hanh vi LIKE keyword y het ban goc (giu nguyen lam fallback).
    std::optional<std::string> searchMemoryByKeyword(const std::string& data);

    // Tim theo cosine similarity, brute-force qua tat ca dong co embedding
    // (bo qua dong embedding NULL). query_embedding la embed() cua "data".
    std::optional<std::string> searchMemoryByVector(const std::string& query,
                                                      const std::vector<float>& query_embedding);

public:
    // C++17: Constructor explicit chống convert ngầm định
    // embedder = nullptr (mặc định) -> KHÔNG breaking code cũ: MemoryTool
    // hoạt động y hệt trước giờ (LIKE keyword search, cột embedding luôn
    // NULL). Truyền vào 1 EmbeddingClient (vd OllamaEmbeddingClient) để
    // bật semantic search.
    explicit MemoryTool(const std::string& db_path = "data/agent_memory.db",
                         std::shared_ptr<EmbeddingClient> embedder = nullptr); // xem nó nên được trỏ vô đâu
    
    ~MemoryTool() override;

    // Ghi đè (override) các hàm từ interface
    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;
};

// Cosine similarity giua 2 vector cung so chieu, brute-force (de bai muc
// nay khong yeu cau index ANN). Free function (khong phai method cua
// MemoryTool) de test file goi truc tiep duoc ma khong can khoi tao ca
// MemoryTool/SQLite - xem tests/memory_tool.cpp. Tra ve 0.0f (thay vi
// throw/UB) neu 2 vector khac so chieu, rong, hoac co norm = 0.
float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);