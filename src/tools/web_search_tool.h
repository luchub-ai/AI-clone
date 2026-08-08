#pragma once

#include "src/tools/tool.h"

#include <optional>
#include <string>

// WebSearchTool: goi Tavily Search API (https://api.tavily.com/search) de
// tim kiem thong tin tren web. (Truoc day goi SearXNG qua GET - da chuyen
// sang Tavily, xem docs/tavily_migration_guide.md.)
// Args dau vao la JSON: {"query": "tu khoa can tim", "num_results": 5, "time_range": "week"}
//   - "num_results" la tuy chon, mac dinh va gioi han toi da boi
//     max_results_ (truyen tu constructor).
//   - "time_range" la tuy chon, chi nhan 1 trong 4 gia tri "day"/"week"/
//     "month"/"year"; bo qua lang le (khong loi) neu LLM truyen gia tri
//     khac hoac khong truyen.
//
// Day la 1 trong 5 tool bat buoc (muc 3.2 de bai).
//
// api_key_: Tavily API key - BAT BUOC, khong co gia tri mac dinh. Noi goi
//   (vd benchmark/run_eval.cpp) doc tu bien moi truong TAVILY_API_KEY roi
//   truyen vao day; class nay KHONG tu doc getenv() de con truyen key gia
//   luc test voi mock server.
// search_api_url_: base URL cua Tavily, mac dinh "https://api.tavily.com".
//   Cho phep ghi de (vd "http://127.0.0.1:19191") de tro toi mock server
//   luc test, khong can goi mang that / khong ton credit that.
class WebSearchTool : public Tool {
public:
    explicit WebSearchTool(std::string api_key,
                            std::string search_api_url = "https://api.tavily.com",
                            int maxResults = 5,
                            int timeoutSeconds = 10);

    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;

private:
    std::string api_key_;
    std::string search_api_url_;
    int max_results_;
    int timeout_seconds_;

    // Ket qua 1 lan goi HTTP toi Tavily.
    //   connected=false : loi mang thuc su (khong ket noi duoc / timeout
    //                      truoc khi nhan duoc byte nao) - KHONG co status_code.
    //   connected=true  : da nhan duoc response tu server, status_code la
    //                      HTTP status that (200, 401, 429, 500...), body
    //                      la noi dung tra ve (JSON, ke ca khi loi).
    struct HttpResult {
        bool connected = false;
        long status_code = 0;
        std::string body;
    };

    std::string getResolvedApiKey() const; // hàm ánh xạ

    // Goi HTTP POST toi {search_api_url_}/search bang libcurl, header
    // "Content-Type: application/json" + "Authorization: Bearer <api_key_>",
    // body la JSON (query/max_results/search_depth/time_range).
    HttpResult httpPostJson(const std::string& json_body) const;

    // Dich HTTP status code (+ doc them field detail.error trong body loi
    // neu co) thanh thong bao de LLM/nguoi doc hieu huong xu ly - xem bang
    // ma loi muc 4.4 trong tavily_migration_guide.md (400/401/403/429/
    // 432/433/500).
    std::string describeHttpError(long status_code, const std::string& body) const;

    // Parse raw JSON tra ve tu Tavily (khi status_code == 200), cat lay toi
    // da num_results ket qua, dinh dang lai thanh text de LLM de doc.
    std::optional<std::string> formatResults(const std::string& raw_json,
                                              int num_results) const;
};