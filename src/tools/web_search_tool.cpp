#include "web_search_tool.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

// Ham callback ma libcurl goi moi khi nhan duoc 1 phan du lieu response.
// Giong het pattern trong ollama_client.cpp / colab_client.cpp - gop du
// lieu vao 1 std::string.
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

WebSearchTool::WebSearchTool(std::string api_key, std::string search_api_url,
                              int maxResults, int timeoutSeconds)
    : api_key_(std::move(api_key)),
      search_api_url_(std::move(search_api_url)),
      max_results_(maxResults),
      timeout_seconds_(timeoutSeconds) {}

std::string WebSearchTool::getResolvedApiKey() const {
    // Nếu key truyền vào lúc khởi tạo đã có -> Dùng luôn
    if (!api_key_.empty()) {
        return api_key_;
    }
    // Nếu lúc khởi tạo bị rỗng -> Tự động ánh xạ lấy từ biến môi trường OS
    const char* env_key = std::getenv("TAVILY_API_KEY");
    return env_key ? std::string(env_key) : "";
}

std::string WebSearchTool::getName() const { return "web_search"; }

std::string WebSearchTool::getDescription() const {
    return "Cong cu tim kiem thong tin tren Internet (qua Tavily Search API). "
           "Tham so dau vao (args) phai la JSON hop le: "
           "{\"query\": \"tu khoa can tim\", \"num_results\": 5, \"time_range\": \"week\"}. "
           "'num_results' la tuy chon, mac dinh va toi da " +
           std::to_string(max_results_) +
           " ket qua. 'time_range' la tuy chon, chi nhan 1 trong 4 gia tri "
           "\"day\"/\"week\"/\"month\"/\"year\" de gioi han ket qua theo do moi "
           "(bo qua neu khong can loc theo thoi gian). Dung tool nay khi can "
           "thong tin moi/thoi su/khong co trong kien thuc san co.";
}

WebSearchTool::HttpResult WebSearchTool::httpPostJson(const std::string& json_body) const {
    HttpResult result;  // connected=false mac dinh

    CURL* curl = curl_easy_init();
    if (!curl) {
        return result;
    }

    std::string read_buffer;
    std::string full_url = search_api_url_ + "/search";

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AI-Agent-Framework/1.0");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // std::string auth_header = "Authorization: Bearer " + api_key_;
    std::string auth_header = "Authorization: Bearer " + getResolvedApiKey();

    headers = curl_slist_append(headers, auth_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        // Loi mang thuc su (khong ket noi duoc / DNS / timeout...) - chua
        // co response nao tu Tavily de ma doc status code.
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    result.connected = true;
    result.status_code = http_code;
    result.body = std::move(read_buffer);
    return result;
}

std::string WebSearchTool::describeHttpError(long status_code, const std::string& body) const {
    // Tavily thuong tra loi kem chi tiet o "detail.error" trong body JSON -
    // doc them cho ro neu co, khong co cung khong sao (van tra loi duoc
    // dua tren status_code).
    std::string detail;
    try {
        json j = json::parse(body);
        if (j.contains("detail") && j["detail"].is_object() &&
            j["detail"].contains("error") && j["detail"]["error"].is_string()) {
            detail = j["detail"]["error"].get<std::string>();
        }
    } catch (const json::exception&) {
        // body khong phai JSON hop le (vd trang loi HTML) - bo qua.
    }
    std::string suffix = detail.empty() ? "" : (" (chi tiet tu Tavily: " + detail + ")");

    switch (status_code) {
        case 400:
            return "Loi: Tavily bao request sai (400) - kiem tra lai cach build request." + suffix;
        case 401:
            return "Loi: Tavily tu choi vi thieu/sai API key (401) - kiem tra bien moi "
                   "truong TAVILY_API_KEY da set dung chua." + suffix;
        case 403:
        case 432:
        case 433:
            // Ca 3 ma nay deu la "khong co quyen goi" theo cach phan loai
            // cua chinh Tavily Python SDK (gop chung 1 nhom ForbiddenError):
            // 432 = het quota goi thang, 433 = vuot gioi han pay-as-you-go,
            // 403 = khong du quyen cho tinh nang dang goi.
            return "Loi: Tavily tu choi request (HTTP " + std::to_string(status_code) +
                   ") - nhieu kha nang la het quota 1000 credit/thang hoac vuot gioi han "
                   "pay-as-you-go, kiem tra dashboard Tavily." + suffix;
        case 429:
            return "Loi: Tavily gioi han toc do goi (429 - rate limit) - thu lai sau it lau." + suffix;
        case 500:
            return "Loi: Tavily bao loi phia server (500) - khong phai loi cua minh, co the thu lai." + suffix;
        default:
            return "Loi: Tavily tra ve HTTP " + std::to_string(status_code) + "." + suffix;
    }
}

std::optional<std::string> WebSearchTool::formatResults(const std::string& raw_json,
                                                          int num_results) const {
    json j;
    try {
        j = json::parse(raw_json);
    } catch (const json::parse_error& e) {
        return std::string("Loi parse JSON tra ve tu Tavily: ") + e.what();
    }

    if (!j.contains("results") || !j["results"].is_array()) {
        return std::string("Khong tim thay truong 'results' trong response cua Tavily.");
    }

    const auto& results = j["results"];
    if (results.empty()) {
        return std::string("Khong tim thay ket qua nao.");
    }

    // [Giu nguyen tinh than fix Bug B]: bao rong json::exception quanh toan
    // bo qua trinh doc field. Tavily la nguon NGOAI, khong kiem soat duoc,
    // 1 item co field sai kieu khong duoc lam crash ca ket qua tim kiem.
    try {
        std::ostringstream oss;
        int count = 0;
        for (const auto& item : results) {
            if (count >= num_results) break;

            std::string title = (item.contains("title") && item["title"].is_string())
                                     ? item["title"].get<std::string>()
                                     : "(khong co tieu de)";
            std::string url = (item.contains("url") && item["url"].is_string())
                                   ? item["url"].get<std::string>()
                                   : "";
            std::string content = (item.contains("content") && item["content"].is_string())
                                       ? item["content"].get<std::string>()
                                       : "(khong co mo ta)";

            oss << (count + 1) << ". " << title << "\n";
            oss << "   URL: " << url << "\n";
            if (item.contains("score") && item["score"].is_number()) {
                oss << "   Do lien quan (score): " << std::fixed << std::setprecision(2)
                    << item["score"].get<double>() << "\n";
            }
            oss << "   Noi dung: " << content << "\n\n";
            ++count;
        }

        return oss.str();
    } catch (const json::exception& e) {
        return std::string("Loi doc du lieu ket qua tu Tavily: ") + e.what();
    }
}

std::optional<std::string> WebSearchTool::execute(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } 
    // C++26: Dùng '_' thay vì 'e' để khai báo biến vô danh
    catch (const json::parse_error& _) { 
        // Ta không cần in chi tiết lỗi của nlohmann, chỉ cần báo cho LLM
        return "Loi parse JSON dau vao: ";
    }

    if (!j.contains("query") || !j["query"].is_string()) {
        return std::string("Loi: JSON args thieu truong 'query' kieu string.");
    }
    std::string query = j["query"].get<std::string>();
    if (query.empty()) {
        return std::string("Loi: 'query' rong.");
    }

    int num_results = max_results_;
    if (j.contains("num_results") && j["num_results"].is_number_integer()) {
        num_results = std::min(j["num_results"].get<int>(), max_results_);
        if (num_results <= 0) num_results = max_results_;
    }

    std::string time_range;
    if (j.contains("time_range") && j["time_range"].is_string()) {
        std::string tr = j["time_range"].get<std::string>();
        static constexpr std::array<const char*, 4> kAllowed = {"day", "week", "month", "year"};
        if (std::find(kAllowed.begin(), kAllowed.end(), tr) != kAllowed.end()) {
            time_range = tr;
        }
        // Gia tri khong hop le -> bo qua lang le, khong loi ca task chi vi
        // 1 tham so tuy chon LLM truyen sai.
    }

    // if (api_key_.empty()) {
    //     return std::string("Loi: chua cau hinh TAVILY_API_KEY cho WebSearchTool.");
    // }

    std::string active_api_key = api_key_;

    // Nếu lúc khởi tạo tool key bị rỗng -> Thử đọc lại từ hệ điều hành (phòng trường hợp .env mới được nạp sau)
    if (active_api_key.empty()) {
        const char* env_key = std::getenv("TAVILY_API_KEY");
        if (env_key) {
            active_api_key = std::string(env_key);
        }
    }

    // -> THAY THẾ TOÀN BỘ ĐOẠN CHECK KEY CỒNG KỀNH BAN NÃY BẰNG 4 DÒNG NÀY:
    if (getResolvedApiKey().empty()) {
        return std::string("Loi: chua cau hinh TAVILY_API_KEY cho WebSearchTool.");
    }

    json request_body = {
        {"query", query},
        {"max_results", num_results},
        {"search_depth", "basic"},  // co dinh "basic" (1 credit) - xem muc 4.2 tavily_migration_guide.md
    };
    if (!time_range.empty()) {
        request_body["time_range"] = time_range;
    }

    auto http_result = httpPostJson(request_body.dump());

    if (!http_result.connected) {
        return std::string("Loi: khong ket noi duoc toi Tavily tai '") +
               search_api_url_ + "' (kiem tra mang / TAVILY_API_KEY, hoac bi timeout sau " +
               std::to_string(timeout_seconds_) + "s).";
    }

    if (http_result.status_code != 200) {
        return describeHttpError(http_result.status_code, http_result.body);
    }

    return formatResults(http_result.body, num_results);
}
