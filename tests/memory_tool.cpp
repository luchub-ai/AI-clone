#include <iostream>
#include <cassert>
#include <string>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include "src/tools/memory_tool.h"
#include "src/client/embedding_client.h"
#include "src/client/ollama_embedding_client.h"
#include "tests/tavily_mock_server.h"

// Hàm tiện ích để in kết quả test dễ nhìn hơn
void printTestResult(const std::string& test_name, const std::optional<std::string>& result) {
    std::cout << "--------------------------------------------------\n";
    std::cout << "[TEST] " << test_name << "\n";
    if (result.has_value()) {
        std::cout << ">> KẾT QUẢ: \n" << result.value() << "\n";
    } else {
        std::cout << ">> KẾT QUẢ: [Thất bại / nullopt]\n";
    }
    std::cout << "--------------------------------------------------\n\n";
}

// ════════════════════════════════════════════════════════════════════
// TEST DOUBLES cho Persistent Memory + Vector Search
// ════════════════════════════════════════════════════════════════════

// Trả về vector CỐ ĐỊNH do test tự định nghĩa qua 1 bảng ánh xạ text->vector
// (không gọi mạng thật) - dùng để test thứ tự ranking save/search một cách
// hoàn toàn xác định (deterministic), không phụ thuộc Ollama có đang chạy
// hay không. Text không có trong bảng ánh xạ -> trả về default_vector_
// (mặc định trực giao với mọi vector "thật" mà test dùng, để không vô tình
// trùng khớp và làm sai kết quả assert).
class FakeEmbeddingClient : public EmbeddingClient {
public:
    explicit FakeEmbeddingClient(std::unordered_map<std::string, std::vector<float>> fixed_vectors,
                                  std::vector<float> default_vector = {})
        : fixed_vectors_(std::move(fixed_vectors)), default_vector_(std::move(default_vector)) {}

    std::optional<std::vector<float>> embed(const std::string& text) override {
        auto it = fixed_vectors_.find(text);
        if (it != fixed_vectors_.end()) return it->second;
        return default_vector_;
    }

private:
    std::unordered_map<std::string, std::vector<float>> fixed_vectors_;
    std::vector<float> default_vector_;
};

// Luôn trả về nullopt - mô phỏng Ollama tắt/crash/timeout giữa chừng, dùng
// để test graceful fallback (KHÔNG phải trường hợp embedder_ == nullptr).
class AlwaysFailEmbeddingClient : public EmbeddingClient {
public:
    std::optional<std::vector<float>> embed(const std::string&) override {
        return std::nullopt;
    }
};

// ════════════════════════════════════════════════════════════════════
// TIER 1: Unit test thuần cho cosineSimilarity() - không cần DB, không
// cần Ollama, cực nhanh, chạy lặp lại bao nhiêu lần cũng được.
// ════════════════════════════════════════════════════════════════════
void testCosineSimilarityUnit() {
    std::cout << "\n===== [TIER 1] cosineSimilarity() - unit test thuần =====\n";

    float orthogonal = cosineSimilarity({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    std::cout << "Hai vector vuong goc: " << orthogonal << " (ky vong ~0.0)\n";
    assert(std::fabs(orthogonal - 0.0f) < 1e-5f);

    float identical = cosineSimilarity({1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f});
    std::cout << "Hai vector giong het nhau: " << identical << " (ky vong ~1.0)\n";
    assert(std::fabs(identical - 1.0f) < 1e-4f);

    float scaled = cosineSimilarity({1.0f, 2.0f, 3.0f}, {2.0f, 4.0f, 6.0f});
    std::cout << "Hai vector cung huong, khac do lon: " << scaled << " (ky vong ~1.0, cosine bat bien theo scale)\n";
    assert(std::fabs(scaled - 1.0f) < 1e-4f);

    float opposite = cosineSimilarity({1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    std::cout << "Hai vector nguoc huong: " << opposite << " (ky vong ~-1.0)\n";
    assert(std::fabs(opposite - (-1.0f)) < 1e-4f);

    // Edge case an toan: khong throw/UB khi vector rong hoac khac so chieu
    float empty_case = cosineSimilarity({}, {1.0f, 2.0f});
    assert(std::fabs(empty_case - 0.0f) < 1e-9f);

    float mismatched_size = cosineSimilarity({1.0f, 2.0f}, {1.0f, 2.0f, 3.0f});
    assert(std::fabs(mismatched_size - 0.0f) < 1e-9f);

    float zero_vector = cosineSimilarity({0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f});
    assert(std::fabs(zero_vector - 0.0f) < 1e-9f); // tránh chia 0 / NaN

    std::cout << "[PASS] cosineSimilarity() đúng cho mọi trường hợp (vuông góc/giống hệt/cùng "
                 "hướng khác scale/ngược hướng/vector rỗng/khác số chiều/vector-không).\n";
}

// ════════════════════════════════════════════════════════════════════
// TIER 2: FakeEmbeddingClient - test thứ tự ranking save/search đúng mà
// KHÔNG cần Ollama sống. Rẻ và deterministic -> nên là test chạy lặp lại
// nhiều nhất trong bộ test này.
// ════════════════════════════════════════════════════════════════════
void testVectorSearchRankingWithFake() {
    std::cout << "\n===== [TIER 2] MemoryTool + FakeEmbeddingClient - test ranking =====\n";

    const std::string db_path = "test_memory_vector_ranking.db";
    std::remove(db_path.c_str());

    // 3 "ký ức" với vector tự chọn tay: "mèo" và "chó" cùng hướng chủ đề
    // thú cưng (dog gần cat hơn rain), "mưa" thuộc chủ đề khác hẳn.
    auto fake = std::make_shared<FakeEmbeddingClient>(
        std::unordered_map<std::string, std::vector<float>>{
            {"Con mèo đen đang ngủ trên ghế sofa",      {1.0f, 0.0f, 0.0f}},
            {"Con chó vàng chạy trong công viên",       {0.9f, 0.1f, 0.0f}},
            {"Hôm nay trời mưa rất to ở Sài Gòn",       {0.0f, 1.0f, 0.0f}},
            {"loài vật nuôi trong nhà",                 {1.0f, 0.0f, 0.0f}}, // query, cùng hướng "mèo"
        });

    MemoryTool tool(db_path, fake);

    printTestResult("Lưu ký ức 1 (mèo)", tool.execute(R"({"action":"save","data":"Con mèo đen đang ngủ trên ghế sofa"})"));
    printTestResult("Lưu ký ức 2 (chó)", tool.execute(R"({"action":"save","data":"Con chó vàng chạy trong công viên"})"));
    printTestResult("Lưu ký ức 3 (mưa)", tool.execute(R"({"action":"save","data":"Hôm nay trời mưa rất to ở Sài Gòn"})"));

    auto search_result = tool.execute(R"({"action":"search","data":"loài vật nuôi trong nhà"})");
    printTestResult("Tìm kiếm ngữ nghĩa (query: loài vật nuôi trong nhà)", search_result);

    assert(search_result.has_value());
    size_t pos_cat  = search_result->find("mèo");
    size_t pos_dog  = search_result->find("chó");
    size_t pos_rain = search_result->find("mưa");
    assert(pos_cat != std::string::npos && pos_dog != std::string::npos && pos_rain != std::string::npos);
    // Ký ức càng liên quan (cosine cao hơn) phải xuất hiện càng sớm trong kết quả.
    assert(pos_cat < pos_dog);
    assert(pos_dog < pos_rain);

    std::cout << "[PASS] Thứ tự ranking đúng: mèo (sim~1.0) > chó (sim~0.994) > mưa (sim~0.0).\n";
    std::remove(db_path.c_str());
}

// ════════════════════════════════════════════════════════════════════
// TIER 2b: Edge case bắt buộc - DB cũ (chưa có cột embedding) mở lên
// không được vỡ dữ liệu, và dòng embedding NULL phải bị bỏ qua an toàn
// khi tính cosine (không segfault).
// ════════════════════════════════════════════════════════════════════
void testMigrationAndNullEmbeddingSkip() {
    std::cout << "\n===== [TIER 2b] Migration DB cũ + bỏ qua dòng embedding NULL =====\n";

    const std::string db_path = "test_memory_migration.db";
    std::remove(db_path.c_str());

    // Giả lập DB được tạo TRƯỚC KHI có feature này: schema cũ 3 cột, tự tay
    // chèn 1 dòng bằng raw sqlite3 API (không đi qua MemoryTool).
    sqlite3* raw_db = nullptr;
    assert(sqlite3_open(db_path.c_str(), &raw_db) == SQLITE_OK);
    const char* old_schema_sql = R"(
        CREATE TABLE memories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            content TEXT NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";
    char* err = nullptr;
    assert(sqlite3_exec(raw_db, old_schema_sql, nullptr, nullptr, &err) == SQLITE_OK);
    assert(sqlite3_exec(raw_db,
        "INSERT INTO memories (content) VALUES ('Dữ liệu cũ trước khi có migration');",
        nullptr, nullptr, &err) == SQLITE_OK);
    sqlite3_close(raw_db);

    auto fake = std::make_shared<FakeEmbeddingClient>(
        std::unordered_map<std::string, std::vector<float>>{
            {"Nội dung mới có embedding", {1.0f, 0.0f}},
        },
        /*default_vector=*/std::vector<float>{0.5f, 0.5f});

    // Mở DB cũ bằng MemoryTool mới (có embedder) -> phải tự migrate (thêm
    // cột embedding), KHÔNG được vỡ dữ liệu cũ.
    MemoryTool tool(db_path, fake);

    // Toàn vẹn dữ liệu: kiểm tra trực tiếp bằng raw SQL, độc lập với logic
    // search (search sẽ đi vector-mode vì fake luôn trả vector hợp lệ, nên
    // dòng cũ - không có embedding - sẽ không xuất hiện ở đó, đây là hành
    // vi ĐÚNG theo thiết kế chứ không phải mất dữ liệu - xem test bên dưới).
    sqlite3* check_db = nullptr;
    assert(sqlite3_open(db_path.c_str(), &check_db) == SQLITE_OK);
    sqlite3_stmt* check_stmt;
    assert(sqlite3_prepare_v2(check_db,
        "SELECT content FROM memories WHERE content = 'Dữ liệu cũ trước khi có migration';",
        -1, &check_stmt, nullptr) == SQLITE_OK);
    assert(sqlite3_step(check_stmt) == SQLITE_ROW);
    sqlite3_finalize(check_stmt);
    sqlite3_close(check_db);
    std::cout << "[PASS] Migration không làm vỡ/mất dữ liệu cũ (kiểm tra bằng raw SQL).\n";

    // Dữ liệu cũ vẫn phải tìm được qua keyword search - mở 1 MemoryTool
    // KHÁC (không có embedder) trỏ vào CÙNG file DB đã migrate, xác nhận
    // nhánh code cũ (LIKE) không bị hỏng bởi migration.
    MemoryTool tool_keyword_only(db_path);
    auto search_old = tool_keyword_only.execute(R"({"action":"search","data":"Dữ liệu cũ"})");
    assert(search_old.has_value());
    assert(search_old->find("Dữ liệu cũ trước khi có migration") != std::string::npos);
    std::cout << "[PASS] Dữ liệu cũ vẫn tìm được qua keyword search trên DB đã migrate.\n";

    // Lưu 1 dòng MỚI (sẽ có embedding). DB giờ có 1 dòng NULL (cũ) + 1 dòng
    // có embedding (mới) - vector search KHÔNG ĐƯỢC segfault vì dòng NULL.
    printTestResult("Lưu ký ức mới (có embedding)", tool.execute(R"({"action":"save","data":"Nội dung mới có embedding"})"));

    auto search_vec = tool.execute(R"({"action":"search","data":"Nội dung mới có embedding"})");
    printTestResult("Semantic search trên DB có lẫn dòng cũ (NULL) + dòng mới", search_vec);
    assert(search_vec.has_value()); // quan trọng nhất: không nullopt do crash/segfault
    assert(search_vec->find("Nội dung mới có embedding") != std::string::npos);
    std::cout << "[PASS] Vector search chạy an toàn dù DB có dòng embedding NULL (không segfault).\n";

    // Mở lại MemoryTool THÊM 1 LẦN NỮA trên DB đã migrate - PRAGMA check
    // phải nhận ra cột đã tồn tại, không được lỗi "duplicate column name".
    MemoryTool tool_reopen(db_path, fake);
    auto search_again = tool_reopen.execute(R"({"action":"search","data":"Dữ liệu cũ"})");
    assert(search_again.has_value());
    std::cout << "[PASS] Mở lại MemoryTool lần 2 trên DB đã migrate không lỗi 'duplicate column'.\n";

    std::remove(db_path.c_str());
}

// ════════════════════════════════════════════════════════════════════
// TIER 2c: embed() trả nullopt (Ollama "tắt") giữa chừng - saveMemory vẫn
// phải lưu được text, searchMemory phải tự fallback về LIKE keyword.
// ════════════════════════════════════════════════════════════════════
void testEmbedFailureGracefulFallback() {
    std::cout << "\n===== [TIER 2c] embed() luôn thất bại (nullopt) - graceful fallback =====\n";

    const std::string db_path = "test_memory_embed_fail.db";
    std::remove(db_path.c_str());

    auto failing = std::make_shared<AlwaysFailEmbeddingClient>();
    MemoryTool tool(db_path, failing); // embedder_ != nullptr, nhưng embed() luôn nullopt

    auto save_result = tool.execute(R"({"action":"save","data":"Mật khẩu wifi là Oophome2026"})");
    printTestResult("Save khi embed() luôn thất bại", save_result);
    assert(save_result.has_value()); // KHÔNG được crash, vẫn lưu text thành công

    auto search_result = tool.execute(R"({"action":"search","data":"Mật khẩu wifi"})");
    printTestResult("Search khi embed() luôn thất bại (phải tự fallback LIKE)", search_result);
    assert(search_result.has_value());
    assert(search_result->find("Mật khẩu wifi là Oophome2026") != std::string::npos);

    std::cout << "[PASS] embed() fail giữa chừng: saveMemory vẫn lưu text, searchMemory tự fallback về keyword.\n";
    std::remove(db_path.c_str());
}

// ════════════════════════════════════════════════════════════════════
// BONUS (thêm ngoài 3 tier đề yêu cầu): xác nhận OllamaEmbeddingClient
// parse ĐÚNG request/response HTTP thật, dùng lại tests/tavily_mock_server.h
// (mock POSIX-socket server có sẵn trong repo, dùng cho WebSearchTool) -
// KHÔNG cần Ollama sống nhưng vẫn kiểm tra được đúng tầng HTTP/JSON, đặc
// biệt là điểm dễ sai nhất: field "embeddings" số nhiều, mảng của mảng.
// Chạy nhanh + deterministic nên có thể lặp lại thoải mái như Tier 2.
// ════════════════════════════════════════════════════════════════════
void testOllamaEmbedHttpRoundtripWithMockServer() {
    std::cout << "\n===== [BONUS] OllamaEmbeddingClient - round-trip HTTP qua mock server =====\n";

    // Case 1: response đúng định dạng mới ("embeddings": [[...]])
    {
        MockTavilyServer server(19301);
        server.respondWith(200, R"({"model":"nomic-embed-text","embeddings":[[0.1,0.2,0.3,-0.5]]})");
        assert(server.start());
        OllamaEmbeddingClient client("http://127.0.0.1:19301", "nomic-embed-text");
        auto result = client.embed("xin chào");
        server.stop();

        assert(result.has_value());
        assert(result->size() == 4);
        assert(std::fabs((*result)[0] - 0.1f) < 1e-5f);
        assert(std::fabs((*result)[3] - (-0.5f)) < 1e-5f);

        std::string req = server.lastRequestRaw();
        assert(req.find("/api/embed") != std::string::npos);
        assert(req.find("\"model\":\"nomic-embed-text\"") != std::string::npos);
        std::cout << "[PASS] Parse đúng field 'embeddings' số nhiều (mảng của mảng); request đúng endpoint /api/embed.\n";
    }

    // Case 2: response SAI định dạng (field "embedding" số ít - endpoint CŨ)
    {
        MockTavilyServer server(19302);
        server.respondWith(200, R"({"embedding":[0.1,0.2,0.3]})");
        assert(server.start());
        OllamaEmbeddingClient client("http://127.0.0.1:19302", "nomic-embed-text");
        auto result = client.embed("test");
        server.stop();
        assert(!result.has_value()); // PHẢI từ chối, không "đoán mò"
        std::cout << "[PASS] Định dạng cũ 'embedding' số ít bị từ chối đúng cách (nullopt), không crash.\n";
    }

    // Case 3: HTTP 500
    {
        MockTavilyServer server(19303);
        server.respondWith(500, R"({"error":"model not found"})");
        assert(server.start());
        OllamaEmbeddingClient client("http://127.0.0.1:19303", "nomic-embed-text");
        auto result = client.embed("test");
        server.stop();
        assert(!result.has_value());
        std::cout << "[PASS] HTTP 500 -> nullopt, không crash.\n";
    }

    // Case 4: JSON hỏng
    {
        MockTavilyServer server(19304);
        server.respondWith(200, "{not valid json!!!");
        assert(server.start());
        OllamaEmbeddingClient client("http://127.0.0.1:19304", "nomic-embed-text");
        auto result = client.embed("test");
        server.stop();
        assert(!result.has_value());
        std::cout << "[PASS] JSON hỏng -> nullopt, không crash.\n";
    }
}

// ════════════════════════════════════════════════════════════════════
// TIER 3: Test tích hợp THẬT với Ollama + nomic-embed-text đã pull. Chỉ
// nên chạy 1-2 lần (chậm, phụ thuộc mạng/model đã pull hay chưa) - KHÔNG
// assert cứng nếu Ollama không sẵn sàng, chỉ báo SKIP để không làm hỏng
// các lần chạy test khác (vd trên máy chấm bài chưa cài Ollama).
// ════════════════════════════════════════════════════════════════════
void testRealOllamaIntegration() {
    std::cout << "\n===== [TIER 3] Test tích hợp thật với Ollama (chạy 1 lần, có thể SKIP) =====\n";

    OllamaEmbeddingClient real_client("http://localhost:11434", "nomic-embed-text");
    auto result = real_client.embed("Xin chào, đây là bài test tích hợp thật với Ollama.");

    if (!result.has_value()) {
        std::cout << "[SKIP] Không embed được - Ollama chưa chạy tại localhost:11434 hoặc chưa "
                     "`ollama pull nomic-embed-text`. Chạy `ollama serve` + pull model rồi test lại "
                     "để xác nhận round-trip HTTP thật.\n";
        return;
    }

    std::cout << "Số chiều vector nhận được: " << result->size() << " (nomic-embed-text: kỳ vọng 768)\n";
    assert(!result->empty());

    // Sanity check: embed lại đúng câu đó lần nữa phải cho cosine ~1.0
    // (không nhất thiết TUYỆT ĐỐI = 1.0 nếu server có non-determinism nhỏ,
    // nhưng phải rất cao).
    auto result2 = real_client.embed("Xin chào, đây là bài test tích hợp thật với Ollama.");
    if (result2.has_value()) {
        float self_sim = cosineSimilarity(*result, *result2);
        std::cout << "Cosine similarity giữa 2 lần embed cùng 1 câu: " << self_sim << " (kỳ vọng gần 1.0)\n";
        assert(self_sim > 0.99f);
    }

    std::cout << "[PASS] Round-trip HTTP thật với Ollama hoạt động đúng.\n";
}

int main() {
    // ────────────────────────────────────────────────────────────────
    // Phần MỚI: Persistent Memory + Vector Search (+4đ). Chạy theo thứ tự
    // rẻ -> đắt: unit test thuần trước, rồi fake-based (lặp lại được nhiều
    // nhất), rồi các edge case bắt buộc, cuối cùng mới đụng Ollama thật.
    // ────────────────────────────────────────────────────────────────
    testCosineSimilarityUnit();
    testVectorSearchRankingWithFake();
    testMigrationAndNullEmbeddingSkip();
    testEmbedFailureGracefulFallback();
    testOllamaEmbedHttpRoundtripWithMockServer();
    testRealOllamaIntegration();

    std::cout << "\n\n";
    // ────────────────────────────────────────────────────────────────
    // Phần CŨ: giữ nguyên xi, không sửa gì - đảm bảo hành vi keyword LIKE
    // (embedder_ == nullptr, tương thích ngược) vẫn đúng như trước giờ.
    // ────────────────────────────────────────────────────────────────
    std::cout << "BẮT ĐẦU TEST MEMORY TOOL...\n\n";

    // Khởi tạo tool với một file database dành riêng cho test
    MemoryTool memory_tool("test_agent_memory.db");

    // 1. Kiểm tra thông tin Tool
    std::cout << "Tên Tool: " << memory_tool.getName() << "\n";
    std::cout << "Mô tả Tool:\n" << memory_tool.getDescription() << "\n\n";

    // 2. Test LƯU thông tin hợp lệ
    std::string save_args_1 = R"({"action": "save", "data": "Mật khẩu nhà là 'Oophome2026!@#'"})";
    auto result_save_1 = memory_tool.execute(save_args_1);
    printTestResult("Lưu thông tin 1", result_save_1);

    std::string save_args_2 = R"({"action": "save", "data": "Tên của user là 'Nguyễn Văn A'"})";
    auto result_save_2 = memory_tool.execute(save_args_2);
    printTestResult("Lưu thông tin 2", result_save_2);

    // 3. Test TÌM KIẾM thông tin CÓ TỒN TẠI
    std::string search_args_1 = R"({"action": "search", "data": "Mật khẩu nhà, chìa khóa nhà, cửa sổ"})";
    auto result_search_1 = memory_tool.execute(search_args_1);
    printTestResult("Tìm kiếm thông tin (Có tồn tại)", result_search_1);

    // 4. Test TÌM KIẾM thông tin KHÔNG TỒN TẠI
    std::string search_args_2 = R"({"action": "search", "data": "Chìa khóa nhà"})";
    auto result_search_2 = memory_tool.execute(search_args_2);
    printTestResult("Tìm kiếm thông tin (Không tồn tại)", result_search_2);

    // 5. Test XỬ LÝ LỖI: JSON sai định dạng (mô phỏng LLM bị "ngáo")
    std::string bad_json_args = R"({action: "save", data: "Thiếu ngoặc kép ở key"})";
    auto result_bad_json = memory_tool.execute(bad_json_args);
    printTestResult("Xử lý lỗi JSON sai cú pháp", result_bad_json);

    // 6. Test XỬ LÝ LỖI: JSON thiếu trường bắt buộc
    std::string missing_field_args = R"({"action": "save"})"; // Thiếu "data"
    auto result_missing_field = memory_tool.execute(missing_field_args);
    printTestResult("Xử lý lỗi thiếu trường data", result_missing_field);

    // 7. Test XỬ LÝ LỖI: Hành động (action) không hợp lệ
    std::string invalid_action_args = R"({"action": "delete", "data": "Xóa ký ức"})";
    auto result_invalid_action = memory_tool.execute(invalid_action_args);
    printTestResult("Xử lý lỗi Action không hợp lệ", result_invalid_action);

    std::cout << "HOÀN THÀNH TEST!\n";
    return 0;
}
