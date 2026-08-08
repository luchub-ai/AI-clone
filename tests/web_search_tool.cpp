#include "src/tools/web_search_tool.h"
#include "tests/tavily_mock_server.h"

#include <iostream>
#include <string>

namespace {
constexpr int kMockPort = 19191;

void printResult(const std::string& label, const std::optional<std::string>& r) {
    std::cout << "=== " << label << " ===\n" << (r ? *r : "(nullopt)") << "\n\n";
}
}  // namespace

int main() {
    std::cout << "Ten Tool: " << WebSearchTool("x").getName() << "\n";
    std::cout << "Mo ta:\n" << WebSearchTool("x").getDescription() << "\n\n";

    // --- 1. Thanh cong (200): tieng Viet co dau + field 'score' ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(200, R"({
            "query": "hoc may tinh OOP la gi",
            "results": [
                {"title": "OOP la gi?", "url": "https://vi.example.com/oop",
                 "content": "Lap trinh huong doi tuong la mot phong cach...", "score": 0.91},
                {"title": "Hoc may tinh", "url": "https://vi.example.com/cs",
                 "content": "Khoa hoc may tinh bao gom nhieu nhanh...", "score": 0.77}
            ],
            "response_time": 0.8
        })");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r1 = tool.execute(R"({"query": "học máy tính OOP là gì", "num_results": 2})");
        printResult("200 OK - tieng Viet co dau + score", r1);
        server.stop();
    }

    // --- 2. num_results mac dinh (khong truyen) ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(200, R"({"results": [{"title":"t","url":"u","content":"c","score":0.5}]})");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r2 = tool.execute(R"({"query": "test"})");
        printResult("200 OK - khong truyen num_results (dung mac dinh)", r2);
        server.stop();
    }

    // --- 3. time_range hop le duoc truyen qua ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(200, R"({"results": [{"title":"t","url":"u","content":"c","score":0.5}]})");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r3 = tool.execute(R"({"query": "tin tuc AI", "time_range": "week"})");
        printResult("200 OK - co time_range hop le ('week')", r3);
        bool sent_time_range = server.lastRequestRaw().find("\"time_range\":\"week\"") != std::string::npos;
        std::cout << "  [kiem tra] request gui di co chua time_range=week: "
                  << (sent_time_range ? "CO" : "KHONG") << "\n\n";
        server.stop();
    }

    // --- 4. time_range khong hop le -> bo qua lang le, khong loi ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(200, R"({"results": [{"title":"t","url":"u","content":"c","score":0.5}]})");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r4 = tool.execute(R"({"query": "test", "time_range": "hom_qua"})");
        printResult("200 OK - time_range khong hop le (bi bo qua)", r4);
        server.stop();
    }

    // --- 5. 401: sai/thieu API key ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(401, R"({"detail": {"error": "Invalid API key"}})");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r5 = tool.execute(R"({"query": "test"})");
        printResult("401 - sai API key", r5);
        server.stop();
    }

    // --- 6. 429: rate limit ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(429, R"({"detail": {"error": "Rate limit exceeded"}})");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r6 = tool.execute(R"({"query": "test"})");
        printResult("429 - rate limit", r6);
        server.stop();
    }

    // --- 7. 432: het quota goi thang ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(432, R"({"detail": {"error": "Plan limit exceeded"}})");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r7 = tool.execute(R"({"query": "test"})");
        printResult("432 - het quota goi thang", r7);
        server.stop();
    }

    // --- 8. 500: loi phia Tavily ---
    {
        MockTavilyServer server(kMockPort);
        server.respondWith(500, R"({"detail": {"error": "Internal error"}})");
        server.start();
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r8 = tool.execute(R"({"query": "test"})");
        printResult("500 - loi server Tavily", r8);
        server.stop();
    }

    // --- 9. Khong ket noi duoc (network-level, khong co gi lang nghe) ---
    {
        WebSearchTool bad_tool("dummy-test-key", "http://127.0.0.1:1", 3, 2);
        auto r9 = bad_tool.execute(R"({"query": "test"})");
        printResult("Khong ket noi duoc toi Tavily (network-level)", r9);
    }

    // --- 10. JSON dau vao khong hop le ---
    {
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r10 = tool.execute(R"(khong phai json)");
        printResult("JSON dau vao loi", r10);
    }

    // --- 11. Thieu field 'query' ---
    {
        WebSearchTool tool("dummy-test-key", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r11 = tool.execute(R"({"num_results": 2})");
        printResult("Thieu field 'query'", r11);
    }

    // --- 12. Chua cau hinh API key (key rong) - khong duoc cham mang ---
    {
        WebSearchTool tool("", "http://127.0.0.1:" + std::to_string(kMockPort), 3, 5);
        auto r12 = tool.execute(R"({"query": "test"})");
        printResult("API key rong (chua cau hinh)", r12);
    }

    return 0;
}
