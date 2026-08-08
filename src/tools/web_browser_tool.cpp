#include "web_browser_tool.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace {

// So sanh khong phan biet hoa/thuong - URL scheme hop le ca khi viet hoa
// (vd "HTTP://..." theo RFC 3986 scheme khong phan biet hoa/thuong), nhung
// std::string::starts_with lai phan biet, nen can ham rieng thay vi dung
// thang no cho phan kiem tra scheme.
bool startsWithCaseInsensitive(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), s.begin(), [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

}  // namespace

// Giong het WriteCallback trong web_search_tool.cpp / ollama_client.cpp -
// gop du lieu response cua libcurl vao 1 std::string.
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

WebBrowserTool::WebBrowserTool(std::string driver_url, std::string browserBinaryPath,
                                int timeoutSeconds, int textPreviewChars, int maxTextChars,
                                BrowserType browser)
    : driver_url_(std::move(driver_url)),
      browser_binary_path_(std::move(browserBinaryPath)),
      timeout_seconds_(timeoutSeconds),
      text_preview_chars_(textPreviewChars),
      max_text_chars_(maxTextChars),
      browser_type_(browser) {}

WebBrowserTool::~WebBrowserTool() {
    closeSession();
}

std::string WebBrowserTool::getName() const { return "browser"; }

std::string WebBrowserTool::getDescription() const {
    return "Cong cu dieu khien 1 trinh duyet that (qua WebDriver/chromedriver) de mo "
           "1 url CU THE va doc noi dung DA render JS - dung khi web_search khong du "
           "(trang can chay JS moi ra noi dung, hoac can noi dung day du hon snippet). "
           "Tham so dau vao (args) phai la JSON hop le, 1 trong 2 dang: "
           "{\"action\": \"navigate\", \"url\": \"https://...\"} de mo 1 trang moi (url phai "
           "bat dau bang http:// hoac https://; tra ve tieu de + doan dau noi dung), hoac "
           "{\"action\": \"read_text\"} de doc lai TOAN "
           "BO noi dung dang hien tren trang HIEN TAI (phai navigate truoc do trong cung "
           "lan agent chay). Session trinh duyet duoc giu xuyen suot nhieu lan goi tool nay "
           "lien tiep trong 1 task, khong phai mo lai tu dau moi lan.";
}

WebBrowserTool::HttpResult WebBrowserTool::httpRequest(const std::string& method,
                                                         const std::string& path,
                                                         const std::string& json_body) const {
    HttpResult result;  // connected=false mac dinh

    CURL* curl = curl_easy_init();
    if (!curl) {
        return result;
    }

    std::string read_buffer;
    std::string full_url = driver_url_ + path;

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
    // Rieng buoc KET NOI (chromedriver co dang lang nghe khong) luon fail
    // nhanh trong 5s, khong phu thuoc timeout_seconds_ dai bao nhieu - tranh
    // treo ca 30s chi de bao "chromedriver chua chay" luc dev/debug.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AI-Agent-Framework/1.0");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    // "GET" khong can set gi them - mac dinh cua libcurl.

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        // Loi mang thuc su (chromedriver chua chay, sai port...) - chua co
        // response nao de doc status code. Day la nhanh loi PHO BIEN NHAT
        // luc dev vi de quen chay `chromedriver` truoc khi chay agent.
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

std::string WebBrowserTool::describeWebDriverError(long status_code, const std::string& body) const {
    std::string detail;
    try {
        json j = json::parse(body);
        if (j.contains("value") && j["value"].is_object()) {
            const auto& v = j["value"];
            std::string err = (v.contains("error") && v["error"].is_string())
                                   ? v["error"].get<std::string>() : "";
            std::string msg = (v.contains("message") && v["message"].is_string())
                                   ? v["message"].get<std::string>() : "";
            detail = err.empty() ? msg : (err + (msg.empty() ? "" : (": " + msg)));
        }
    } catch (const json::exception&) {
        // body khong phai JSON hop le - bo qua, van tra loi duoc dua tren
        // status_code thoi.
    }
    std::string suffix = detail.empty() ? "" : (" (chi tiet tu chromedriver: " + detail + ")");

    switch (status_code) {
        case 404:
            return "Loi: chromedriver bao khong tim thay session/element (404) - session "
                   "co the da bi dong hoac chua duoc tao." + suffix;
        case 400:
            return "Loi: chromedriver bao request sai (400)." + suffix;
        case 500:
        case 501:
            return "Loi: chromedriver/trinh duyet gap loi noi bo (HTTP " +
                   std::to_string(status_code) + ")." + suffix;
        default:
            return "Loi: chromedriver tra ve HTTP " + std::to_string(status_code) + "." + suffix;
    }
}

std::optional<std::string> WebBrowserTool::ensureSession() {
    if (!session_id_.empty()) {
        return std::nullopt;  // da co session roi, khong can tao lai
    }

    // Dung gan tung truong (thay vi 1 initializer-list long nhieu tang) de
    // tranh nlohmann hieu nham cau truoc long sau thanh array-of-pairs -
    // loi de dinh voi JSON long sau nhu the nay.
    //
    // Day la CHO DUY NHAT Chrome va Firefox khac nhau trong ca file nay -
    // moi ham con lai (navigate, execute/sync, xoa session, dich loi) dung
    // chung 100% vi ca 2 deu la WebDriver chuan.
    json capabilities;
    if (browser_type_ == BrowserType::Chrome) {
        capabilities["capabilities"]["alwaysMatch"]["browserName"] = "chrome";
        capabilities["capabilities"]["alwaysMatch"]["goog:chromeOptions"]["args"] =
            json::array({"--headless=new", "--no-sandbox", "--disable-dev-shm-usage",
                          "--disable-gpu"});
        if (!browser_binary_path_.empty()) {
            capabilities["capabilities"]["alwaysMatch"]["goog:chromeOptions"]["binary"] =
                browser_binary_path_;
        }
    } else {  // BrowserType::Firefox
        capabilities["capabilities"]["alwaysMatch"]["browserName"] = "firefox";
        capabilities["capabilities"]["alwaysMatch"]["moz:firefoxOptions"]["args"] =
            json::array({"-headless"});
        if (!browser_binary_path_.empty()) {
            capabilities["capabilities"]["alwaysMatch"]["moz:firefoxOptions"]["binary"] =
                browser_binary_path_;
        }
    }

    auto http_result = httpRequest("POST", "/session", capabilities.dump());

    if (!http_result.connected) {
        return "Loi: khong ket noi duoc toi chromedriver tai '" + driver_url_ +
               "' - kiem tra da chay lenh `chromedriver --port=...` chua.";
    }
    if (http_result.status_code != 200) {
        return "Loi tao session: " + describeWebDriverError(http_result.status_code, http_result.body);
    }

    try {
        json j = json::parse(http_result.body);
        if (j.contains("value") && j["value"].is_object() &&
            j["value"].contains("sessionId") && j["value"]["sessionId"].is_string()) {
            session_id_ = j["value"]["sessionId"].get<std::string>();
            return std::nullopt;
        }
        return std::string("Loi: chromedriver tra ve response tao session khong dung dinh dang.");
    } catch (const json::exception& e) {
        return std::string("Loi parse JSON tao session: ") + e.what();
    }
}

void WebBrowserTool::closeSession() noexcept {
    if (session_id_.empty()) return;
    try {
        httpRequest("DELETE", "/session/" + session_id_, "");
    } catch (...) {
        // best-effort: destructor khong duoc de exception thoat ra ngoai.
    }
    session_id_.clear();
}

std::optional<std::string> WebBrowserTool::handleNavigate(const std::string& url) {
    if (auto err = ensureSession()) {
        return err;
    }

    json nav_body;
    nav_body["url"] = url;
    auto nav_result = httpRequest("POST", "/session/" + session_id_ + "/url", nav_body.dump());

    if (!nav_result.connected) {
        return std::string("Loi: mat ket noi toi chromedriver khi dang navigate.");
    }
    if (nav_result.status_code != 200) {
        return "Loi navigate: " + describeWebDriverError(nav_result.status_code, nav_result.body);
    }

    // Doc tieu de trang de xac nhan da mo dung noi cho LLM de doc.
    auto title_result = httpRequest("GET", "/session/" + session_id_ + "/title", "");
    std::string title = "(khong doc duoc tieu de)";
    if (title_result.connected && title_result.status_code == 200) {
        try {
            json tj = json::parse(title_result.body);
            if (tj.contains("value") && tj["value"].is_string()) {
                title = tj["value"].get<std::string>();
            }
        } catch (const json::exception&) {
            // giu nguyen title mac dinh neu parse loi, khong lam fail ca
            // navigate chi vi khong doc duoc title.
        }
    }

    // Lay luon 1 doan preview noi dung de agent khong phai goi them
    // read_text ngay cho truong hop don gian - giong tinh than tra ve
    // snippet cua WebSearchTool.
    auto preview = handleReadText();
    std::string preview_text = preview.value_or("(khong doc duoc noi dung)");
    if (preview_text.size() > static_cast<size_t>(text_preview_chars_)) {
        preview_text = preview_text.substr(0, static_cast<size_t>(text_preview_chars_)) +
                       "...(con nua, goi action \"read_text\" de doc tiep)";
    }

    return "Da mo: " + title + " (" + url + ")\n\n" + preview_text;
}

std::optional<std::string> WebBrowserTool::handleReadText() {
    if (session_id_.empty()) {
        return std::string("Loi: chua mo trang nao - goi action \"navigate\" truoc.");
    }

    json script_body;
    script_body["script"] = "return document.body ? document.body.innerText : '';";
    script_body["args"] = json::array();

    auto result = httpRequest("POST", "/session/" + session_id_ + "/execute/sync", script_body.dump());

    if (!result.connected) {
        return std::string("Loi: mat ket noi toi chromedriver khi dang doc noi dung.");
    }
    if (result.status_code != 200) {
        return "Loi doc noi dung: " + describeWebDriverError(result.status_code, result.body);
    }

    try {
        json j = json::parse(result.body);
        if (j.contains("value") && j["value"].is_string()) {
            std::string text = j["value"].get<std::string>();
            if (text.empty()) {
                return std::string("(trang khong co noi dung van ban)");
            }
            if (text.size() > static_cast<size_t>(max_text_chars_)) {
                text = text.substr(0, static_cast<size_t>(max_text_chars_)) +
                       "\n...(da cat bot, trang dai hon " + std::to_string(max_text_chars_) +
                       " ky tu)";
            }
            return text;
        }
        return std::string("Loi: chromedriver tra ve response doc noi dung khong dung dinh dang.");
    } catch (const json::exception& e) {
        return std::string("Loi parse JSON noi dung trang: ") + e.what();
    }
}

std::optional<std::string> WebBrowserTool::execute(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } 
    // C++26: Dùng '_' thay vì 'e' để khai báo biến vô danh
    catch (const json::parse_error& _) { 
        // Ta không cần in chi tiết lỗi của nlohmann, chỉ cần báo cho LLM
        return "Loi: parse json dau vao .";
    }

    if (!j.contains("action") || !j["action"].is_string()) {
        return std::string("Loi: JSON args thieu truong 'action' kieu string "
                            "(\"navigate\" hoac \"read_text\").");
    }
    std::string action = j["action"].get<std::string>();

    if (action == "navigate") {
        if (!j.contains("url") || !j["url"].is_string() || j["url"].get<std::string>().empty()) {
            return std::string("Loi: action \"navigate\" can truong 'url' kieu string, khong rong.");
        }
        std::string url = j["url"].get<std::string>();
        // Chi cho http(s) - chan tu phia C++ TRUOC KHI goi chromedriver,
        // khong de trinh duyet tu quyet dinh co mo file://, chrome://...
        // hay khong. Xem giai thich trong web_browser_tool.h.
        if (!startsWithCaseInsensitive(url, "http://") && !startsWithCaseInsensitive(url, "https://")) {
            return std::string("Loi: chi ho tro url bat dau bang \"http://\" hoac "
                                "\"https://\" - tu choi scheme khac vi ly do an toan.");
        }
        return handleNavigate(url);
    }
    if (action == "read_text") {
        return handleReadText();
    }

    return "Loi: action '" + action + "' khong ho tro. Chi ho tro \"navigate\" va \"read_text\".";
}