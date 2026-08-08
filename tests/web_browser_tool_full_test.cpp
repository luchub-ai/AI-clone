// Test mo rong cho WebBrowserTool, dung chung mock WebDriver server voi
// tests/manual_browser_tool_test.cpp (tests/webdriver_mock_server.py) nhung
// KHONG lap lai cac case da co o file do (navigate/read_text co ban, tai su
// dung session, RAII dong session, loi 500/400, chan file://+chrome://,
// gioi han max_text_chars_, ToolRegistry + allow-list). File nay tap trung
// vao cac case CON THIEU:
//   - Loi ket noi that (chromedriver khong lang nghe o port duoc chi)
//   - JSON dau vao hong hoan toan (khong parse duoc)
//   - Thieu truong 'url', hoac 'url' la chuoi rong
//   - Navigate toi 1 url KHONG co trong PAGES cua mock (fallback DEFAULT_PAGE)
//   - Preview (text_preview_chars_) NGAN HON va co dau "...(con nua...)"
//     so voi read_text() day du - 2 tham so nay dang bi nham la mot
//   - read_text() goi 2 lan lien tiep (khong navigate giua 2 lan) phai
//     idempotent - tra ve dung 1 noi dung
//   - Nhieu navigate lien tiep (5 lan) van chi dung 1 session (mo rong quy
//     mo so voi test 2-lan-navigate da co)
//   - Destructor AN TOAN khi chua tung goi navigate (session_id_ rong tu
//     dau) - khong duoc goi DELETE thua, khong duoc crash
//   - BrowserType::Firefox van hoat dong dung qua cung 1 mock (mock khong
//     phan biet goog:chromeOptions/moz:firefoxOptions, chi ham
//     ensureSession() moi phan nhanh 2 loai capabilities khac nhau)
//   - getName()/getDescription() dung noi dung mong doi cho LLM doc
//   - Truong JSON du thua (khong khai bao) khong lam hong parse
//
// Chay (mock server dung CHUNG 1 tien trinh cho ca file nay, dung port
// khac 9515 mac dinh de khong dung do voi manual_browser_tool_test.cpp
// neu ca 2 chay song song luc CI):
// ================================================
//
//   python3 tests/webdriver_mock_server.py 9516 &
//   g++ -std=c++23 -I. -Isrc tests/web_browser_tool_full_test.cpp \
//       src/tools/web_browser_tool.cpp -lcurl -o /tmp/browser_full_test
//   /tmp/browser_full_test
//
// ================================================
// Luu y: nhom test "loi ket noi" (Nhom 1 duoi day) CO CHU DICH khong dung
// mock server - no tro toi 1 port khong ai lang nghe de ep loi ket noi
// that, nen KHONG can chay mock server truoc cho rieng nhom do.
#include "src/tools/web_browser_tool.h"
#include "src/tools/tool_registry.h"

#include <curl/curl.h>

#include <iostream>
#include <memory>
#include <string>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const std::string& label) {
    if (cond) {
        std::cout << "  [OK]   " << label << "\n";
        ++g_pass;
    } else {
        std::cout << "  [FAIL] " << label << "\n";
        ++g_fail;
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Giong fetchDebugState trong manual_browser_tool_test.cpp - goi rieng
// GET /debug/state cua mock, khong qua WebBrowserTool.
struct DebugState {
    long created = -1;
    long active = -1;
};

size_t WriteCb(void* c, size_t sz, size_t nm, std::string* out) {
    out->append(static_cast<char*>(c), sz * nm);
    return sz * nm;
}

DebugState fetchDebugState(const std::string& base_url) {
    DebugState st;
    CURL* curl = curl_easy_init();
    if (!curl) return st;
    std::string buf;
    std::string url = base_url + "/debug/state";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    if (curl_easy_perform(curl) == CURLE_OK) {
        auto p1 = buf.find("\"created_count\":");
        auto p2 = buf.find("\"active_count\":");
        if (p1 != std::string::npos) st.created = std::stol(buf.substr(p1 + 16));
        if (p2 != std::string::npos) st.active = std::stol(buf.substr(p2 + 15));
    }
    curl_easy_cleanup(curl);
    return st;
}

}  // namespace

int main() {
    // Mock server that phai chay o day (xem comment dau file). Doi voi
    // manual_browser_tool_test.cpp dung port 9516 de khong dam vao nhau
    // neu chay song song.
    const std::string kBase = "http://127.0.0.1:9516";
    // Port nay CHU DICH khong co gi lang nghe - dung rieng cho Nhom 1.
    const std::string kDeadPort = "http://127.0.0.1:9599";

    std::cout << "== Nhom 1: loi ket noi that (chromedriver khong chay o port do) ==\n";
    {
        WebBrowserTool tool(kDeadPort);
        auto r = tool.execute(R"({"action":"navigate","url":"https://example.com/machine-learning"})");
        check(r.has_value(), "van tra ve thong bao (khong nullopt, khong throw) khi khong ket noi duoc");
        check(r && contains(*r, "khong ket noi duoc"), "thong bao neu ro la loi KET NOI, khong phai loi trang");
        check(r && contains(*r, kDeadPort), "thong bao co nhac lai driver_url_ de nguoi dung biet dang tro sai dau");
    }

    std::cout << "\n== Nhom 2: getName()/getDescription() ==\n";
    {
        WebBrowserTool tool(kBase);
        check(tool.getName() == "browser", "getName() tra ve dung 'browser'");
        std::string desc = tool.getDescription();
        check(contains(desc, "navigate"), "getDescription() co nhac action 'navigate'");
        check(contains(desc, "read_text"), "getDescription() co nhac action 'read_text'");
        check(contains(desc, "http"), "getDescription() co nhac rang buoc scheme http(s)");
    }

    std::cout << "\n== Nhom 3: JSON dau vao hong hoac thieu truong ==\n";
    {
        WebBrowserTool tool(kBase);

        auto r_bad_json = tool.execute(R"({"action": "navigate", )");  // JSON co y cat cut
        check(r_bad_json && contains(*r_bad_json, "parse JSON dau vao"), "JSON khong hop le -> bao loi parse ro rang, khong crash");

        auto r_no_url = tool.execute(R"({"action":"navigate"})");
        check(r_no_url && contains(*r_no_url, "url"), "thieu truong 'url' cho navigate -> bao loi ro rang");

        auto r_empty_url = tool.execute(R"({"action":"navigate","url":""})");
        check(r_empty_url && contains(*r_empty_url, "khong rong"), "'url' la chuoi rong -> bi tu choi giong nhu thieu truong");

        auto r_url_not_string = tool.execute(R"({"action":"navigate","url":123})");
        check(r_url_not_string && contains(*r_url_not_string, "url"), "'url' sai kieu du lieu (so, khong phai string) -> bao loi, khong crash");

        auto r_extra_field = tool.execute(
            R"({"action":"navigate","url":"https://example.com/machine-learning","note":"khong lien quan"})");
        check(r_extra_field && contains(*r_extra_field, "Machine Learning 101"),
              "truong JSON thua khong khai bao (vd 'note') khong lam hong viec parse args hop le");
    }

    std::cout << "\n== Nhom 4: navigate toi url KHONG co trong PAGES cua mock (fallback) ==\n";
    {
        WebBrowserTool tool(kBase);
        auto r = tool.execute(R"({"action":"navigate","url":"https://example.com/khong-ton-tai"})");
        check(r && contains(*r, "Untitled Mock Page"), "url la nhung khong co trong PAGES -> tra ve DEFAULT_PAGE, khong bao loi");
    }

    std::cout << "\n== Nhom 5: preview (navigate) NGAN HON read_text() day du ==\n";
    {
        // textPreviewChars = 30, con maxTextChars giu mac dinh (8000) - de
        // phan biet ro 2 tham so nay KHONG phai la 1, deu ap dung do dai
        // khac nhau cho 2 tinh huong khac nhau (preview kem theo navigate,
        // vs noi dung day du tra ve tu read_text rieng).
        WebBrowserTool tool(kBase, /*browserBinaryPath=*/"", /*timeoutSeconds=*/30,
                             /*textPreviewChars=*/30, /*maxTextChars=*/8000);

        auto r_nav = tool.execute(R"({"action":"navigate","url":"https://example.com/machine-learning"})");
        check(r_nav && contains(*r_nav, "con nua, goi action"), "preview trong navigate bi cat va co ghi chu doc tiep bang read_text");

        auto r_full = tool.execute(R"({"action":"read_text"})");
        check(r_full && r_full->size() > 30, "read_text() rieng tra ve noi dung DAY DU, khong bi cat theo textPreviewChars");
        check(r_full && !contains(*r_full, "con nua, goi action"),
              "read_text() day du KHONG mang theo ghi chu cat cua preview (2 co che doc lap nhau)");
    }

    std::cout << "\n== Nhom 6: read_text() goi 2 lan lien tiep phai idempotent ==\n";
    {
        WebBrowserTool tool(kBase);
        tool.execute(R"({"action":"navigate","url":"https://example.com/deep-learning"})");
        auto r1 = tool.execute(R"({"action":"read_text"})");
        auto r2 = tool.execute(R"({"action":"read_text"})");
        check(r1.has_value() && r2.has_value() && *r1 == *r2,
              "goi read_text() 2 lan lien tiep (khong navigate giua 2 lan) tra ve GIONG HET nhau");
    }

    std::cout << "\n== Nhom 7: nhieu navigate lien tiep (5 lan) van chi dung 1 session ==\n";
    {
        // Dung CHENH LECH truoc/sau, KHONG dung so tuyet doi: mock server
        // giu 1 bien dem toan cuc (CREATED_COUNT) xuyen suot ca file test
        // nay (cac nhom truoc do da tao vai session roi), nen "created == 1"
        // se sai ngay ca khi hanh vi WebBrowserTool hoan toan dung. Day
        // cung la bay ma ban than test nay gap phai luc viet (xem lai neu
        // sua file: moi assertion dua vao debug/state phai la delta).
        auto before = fetchDebugState(kBase);
        WebBrowserTool tool(kBase);
        for (int i = 0; i < 5; ++i) {
            const std::string url = (i % 2 == 0) ? "https://example.com/machine-learning"
                                                  : "https://example.com/deep-learning";
            tool.execute(R"({"action":"navigate","url":")" + url + R"("})");
        }
        auto after = fetchDebugState(kBase);
        check(after.created - before.created == 1, "5 lan navigate lien tiep tren CUNG 1 instance chi tao THEM 1 session duy nhat");
        check(after.active == before.active + 1, "so session active tang dung 1 (session moi), khong tang theo tung lan navigate");
    }

    std::cout << "\n== Nhom 8: destructor an toan khi CHUA TUNG navigate ==\n";
    {
        auto before = fetchDebugState(kBase);
        {
            WebBrowserTool tool(kBase);
            // Khong goi execute() gi ca - session_id_ van dang rong luc
            // destructor chay.
        }
        auto after = fetchDebugState(kBase);
        check(before.created == after.created, "destructor khi chua navigate KHONG goi tao/dong session thua (session_id_ rong tu dau)");
    }

    std::cout << "\n== Nhom 9: BrowserType::Firefox van hoat dong qua cung 1 mock ==\n";
    {
        // Mock server khong phan biet goog:chromeOptions/moz:firefoxOptions
        // (chi luu url/text theo sessionId) nen navigate van phai thanh
        // cong binh thuong - cho thay tham so BrowserType chi anh huong
        // capabilities luc tao session (xem ensureSession() trong .cpp),
        // khong anh huong logic navigate/read_text con lai.
        WebBrowserTool tool(kBase, /*browserBinaryPath=*/"", /*timeoutSeconds=*/30,
                             /*textPreviewChars=*/1500, /*maxTextChars=*/8000,
                             BrowserType::Firefox);
        auto r = tool.execute(R"({"action":"navigate","url":"https://example.com/machine-learning"})");
        check(r && contains(*r, "Machine Learning 101"), "BrowserType::Firefox van navigate + doc dung tieu de qua mock");
    }

    std::cout << "\n=== KET QUA: " << g_pass << " OK, " << g_fail << " FAIL ===\n";
    return g_fail == 0 ? 0 : 1;
}