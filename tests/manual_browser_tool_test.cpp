// Test end-to-end WebBrowserTool truoc 1 mock WebDriver server (khong can
// Chrome that). Kiem tra ca hanh vi STATEFUL (tai su dung session giua
// nhieu lan navigate, dong session luc destructor) chu khong chi tung
// request doc lap - day la diem khac biet quan trong nhat cua tool nay so
// voi 5 tool con lai.
//
// Chay: python3 tests/webdriver_mock_server.py 9515 &
//       g++ -std=c++23 -I. -Isrc tests/manual_browser_tool_test.cpp
//           src/tools/web_browser_tool.cpp -lcurl -o /tmp/browser_test
//       /tmp/browser_test
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

// Goi truc tiep GET /debug/state cua mock server (KHONG qua WebBrowserTool -
// day la kenh rieng chi test C++ dung, WebDriver that khong co endpoint
// nay) de kiem tra so session da tao / dang con mo.
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
    const std::string kBase = "http://127.0.0.1:9515";

    std::cout << "== Nhom 1: navigate + read_text co ban ==\n";
    {
        WebBrowserTool tool(kBase);

        auto r1 = tool.execute(R"({"action":"navigate","url":"https://example.com/machine-learning"})");
        check(r1.has_value(), "navigate tra ve co gia tri (khong nullopt)");
        check(r1 && contains(*r1, "Machine Learning 101"), "navigate tra ve dung tieu de trang 1");
        check(r1 && contains(*r1, "Machine Learning la mot nhanh"), "navigate tra ve kem preview noi dung trang 1");

        auto r2 = tool.execute(R"({"action":"read_text"})");
        check(r2.has_value() && contains(*r2, "hoc tu du lieu"), "read_text doc dung noi dung day du trang 1");

        std::cout << "-- navigate lan 2, CUNG 1 instance (test tai su dung session) --\n";
        auto r3 = tool.execute(R"({"action":"navigate","url":"https://example.com/deep-learning"})");
        check(r3 && contains(*r3, "Deep Learning Explained"), "navigate lan 2 tra ve dung tieu de trang 2 (session chuyen trang dung)");
        check(r3 && !contains(*r3, "Machine Learning 101"), "navigate lan 2 KHONG con dinh noi dung trang 1 cu");

        auto debug_after_2_navigates = fetchDebugState(kBase);
        check(debug_after_2_navigates.created == 1,
              "chi 1 session duoc TAO cho ca 2 lan navigate lien tiep (session_id_ duoc tai su dung, dung kien truc lazy+stateful)");
        check(debug_after_2_navigates.active == 1, "session dang con active truoc khi tool bi huy");
    }  // <-- tool ra khoi scope o day, destructor phai goi DELETE

    auto debug_after_destructor = fetchDebugState(kBase);
    check(debug_after_destructor.created == 1, "created_count khong doi sau destructor (dung nhu ky vong)");
    check(debug_after_destructor.active == 0, "destructor da dong session that (active_count ve 0) - RAII hoat dong dung");

    std::cout << "\n== Nhom 2: instance thu 2 phai tao session RIENG ==\n";
    {
        WebBrowserTool tool2(kBase);
        tool2.execute(R"({"action":"navigate","url":"https://example.com/machine-learning"})");
        auto st = fetchDebugState(kBase);
        check(st.created == 2, "instance moi tao session moi, khong dung lai session cu da bi dong");
    }

    std::cout << "\n== Nhom 3: xu ly loi tu chromedriver (qua mock 500/400) ==\n";
    {
        WebBrowserTool tool3(kBase);
        auto r500 = tool3.execute(R"({"action":"navigate","url":"http://mock-error.test/trigger-500"})");
        check(r500 && contains(*r500, "loi noi bo"), "loi 500 tu chromedriver duoc dich ra thong bao ro rang");

        WebBrowserTool tool4(kBase);
        auto r400 = tool4.execute(R"({"action":"navigate","url":"http://mock-error.test/trigger-400"})");
        check(r400 && contains(*r400, "request sai"), "loi 400 tu chromedriver duoc dich ra thong bao ro rang");
    }

    std::cout << "\n== Nhom 4: validate dau vao TRUOC KHI cham chromedriver ==\n";
    {
        WebBrowserTool tool5(kBase);
        auto r_no_action = tool5.execute(R"({"foo":"bar"})");
        check(r_no_action && contains(*r_no_action, "action"), "thieu 'action' -> bao loi, khong crash");

        auto r_bad_action = tool5.execute(R"({"action":"click"})");
        check(r_bad_action && contains(*r_bad_action, "khong ho tro"), "action la -> bao loi ro rang");

        auto r_file_scheme = tool5.execute(R"({"action":"navigate","url":"file:///etc/passwd"})");
        check(r_file_scheme && contains(*r_file_scheme, "an toan"), "url file:// bi tu choi TU PHIA C++ (khong goi chromedriver)");

        auto before = fetchDebugState(kBase);
        tool5.execute(R"({"action":"navigate","url":"chrome://settings"})");
        auto after = fetchDebugState(kBase);
        check(before.created == after.created, "url scheme sai KHONG lam tang so session (bi chan truoc khi ensureSession() chay)");

        auto r_read_before_nav = WebBrowserTool(kBase).execute(R"({"action":"read_text"})");
        check(r_read_before_nav && contains(*r_read_before_nav, "goi action"), "read_text truoc navigate -> bao loi ro rang, khong crash");

        // Scheme viet hoa van hop le theo RFC 3986 - fix moi, kiem tra
        // KHONG bi tu choi oan (khac voi file:// hay chrome:// o tren).
        WebBrowserTool tool_upper(kBase);
        auto r_upper_scheme = tool_upper.execute(R"({"action":"navigate","url":"HTTPS://example.com/machine-learning"})");
        check(r_upper_scheme && contains(*r_upper_scheme, "Machine Learning 101"),
              "url voi scheme VIET HOA (HTTPS://) van duoc chap nhan, khong bi tu choi oan");
    }

    std::cout << "\n== Nhom 5: gioi han do dai read_text (max_text_chars_) ==\n";
    {
        // truyen maxTextChars rat nho (50) de ep truncation xay ra chac chan
        WebBrowserTool tool6(kBase, /*browserBinaryPath=*/"", /*timeoutSeconds=*/30,
                              /*textPreviewChars=*/1500, /*maxTextChars=*/50);
        tool6.execute(R"({"action":"navigate","url":"https://example.com/machine-learning"})");
        auto r = tool6.execute(R"({"action":"read_text"})");
        check(r && r->size() < 200, "read_text bi cat khi vuot maxTextChars (khong tra nguyen van ban dai)");
        check(r && contains(*r, "da cat bot"), "read_text co ghi chu da bi cat, LLM biet con thieu noi dung");
    }

    std::cout << "\n== Nhom 6: qua ToolRegistry that (khong goi WebBrowserTool truc tiep nua) ==\n";
    {
        ToolRegistry registry;
        registry.registerTool(std::make_unique<WebBrowserTool>(kBase));

        auto descriptions = registry.getToolDescriptions();
        check(contains(descriptions, "browser:"), "getToolDescriptions() liet ke dung ten 'browser' cho LLM doc");

        auto r = registry.executeTool("browser", R"({"action":"navigate","url":"https://example.com/machine-learning"})");
        check(r && contains(*r, "Machine Learning 101"), "registry.executeTool(\"browser\", ...) dispatch dung toi WebBrowserTool");

        registry.setAllowedTools({"calculator", "web_search"});  // KHONG co "browser"
        auto denied = registry.executeTool("browser", R"({"action":"read_text"})");
        check(!denied.has_value(), "policy allow-list cua ToolRegistry chan duoc 'browser' khi khong nam trong danh sach cho phep");
    }

    std::cout << "\n=== KET QUA: " << g_pass << " OK, " << g_fail << " FAIL ===\n";
    return g_fail == 0 ? 0 : 1;
}