#pragma once

#include "src/tools/tool.h"

#include <optional>
#include <string>

// WebBrowserTool: dieu khien 1 trinh duyet that (qua WebDriver protocol -
// chinh la giao thuc REST ma Selenium dung, chromedriver la server thuc thi
// no) de mo mot url CU THE va doc noi dung DA render JS - khac voi
// WebSearchTool (chi tra ve snippet co san tu index cua Tavily, khong mo
// trang that luc goi tool).
//
// Day la 1 trong 3 tool "tu them" (muc 3.2 de bai, tham khao OpenClaw hoac
// Hermes - ca 2 framework nay deu co san 1 tool "browser" tach rieng khoi
// web search, dung dung y tuong nay).
//
// KHAC BIET QUAN TRONG voi 5 tool bat buoc con lai: day la tool STATEFUL
// dau tien trong project - no giu 1 "session" trinh duyet song xuyen suot
// nhieu lan execute() lien tiep (navigate roi read_text phai cung 1 tab,
// khong phai 2 request doc lap nhu WebSearchTool). Session duoc tao "lazy"
// (chi khi action dau tien can den) va duoc dong tu dong trong destructor
// (RAII) - KHONG can goi action "close" rieng.
//
// YEU CAU HE THONG: can co chromedriver dang chay san truoc (vd
// `chromedriver --port=9515 &`), giong tinh than phai `ollama serve` truoc
// khi chay agent. chromedriver lai can Chrome/Chromium that de dieu khien.
//
// Args dau vao la JSON theo 1 trong 2 dang:
//   {"action": "navigate", "url": "https://..."}
//     -> mo url do trong session (tao session moi neu chua co), tra ve
//        tieu de trang + doan dau noi dung (cat o text_preview_chars_).
//        CHI CHAP NHAN url bat dau bang "http://" hoac "https://" - tu choi
//        cac scheme khac (vd "file://", "chrome://") tu phia C++ TRUOC KHI
//        goi chromedriver, vi 1 LLM dieu khien tool nay ma cho phep mo
//        file cuc bo/URL noi bo la 1 rui ro (agent co the bi noi dung
//        trang web "du" navigate sang cho khac ngoai y muon).
//   {"action": "read_text"}
//     -> doc lai TOAN BO noi dung van ban dang hien tren trang HIEN TAI
//        cua session (khong can url - dung sau navigate, hoac khi trang tu
//        thay doi vi JS chay them, vd lazy-load khi cuon xuong).
//
// driver_url_: base URL cua chromedriver, mac dinh "http://127.0.0.1:9515".
//   Giong tinh than search_api_url_ cua WebSearchTool - cho phep ghi de de
//   tro toi mock WebDriver server luc test, khong can Chrome that.
//
// VE VIEC CHI HO TRO CHROME HAY CA FIREFOX: WebDriver la 1 chuan W3C, KHONG
// rieng cho Chrome. chromedriver (dieu khien Chrome) va geckodriver (dieu
// khien Firefox) noi CUNG 1 "ngon ngu" REST/JSON - navigate, execute/sync,
// dong session deu giong het nhau, va describeWebDriverError() ben duoi
// dung chung duoc cho ca 2 (da tu kiem chung: gui thang 1 request toi
// geckodriver THAT, no tra loi dung dinh dang {"value":{"error":...,
// "message":...}} y het chromedriver). Cho khac nhau DUY NHAT nam o
// capabilities luc tao session: Chrome dung "goog:chromeOptions", Firefox
// dung "moz:firefoxOptions" - xem enum BrowserType va ensureSession() trong
// .cpp. Cong that (chromedriver/geckodriver) mac dinh lang nghe port khac
// nhau (9515 vs 4444) nen doi browser thi nho doi luon driver_url_.
enum class BrowserType { Chrome, Firefox };

class WebBrowserTool : public Tool {
public:
    // browserBinaryPath: duong dan toi file thuc thi trinh duyet (Chrome
    // for Testing hoac Firefox). De trong "" thi driver tu do tim trinh
    // duyet theo PATH/vi tri mac dinh cua he thong - neu trinh duyet khong
    // nam trong PATH (truong hop pho bien voi Chrome for Testing, vi day
    // khong phai cai qua apt) thi NEN truyen duong dan ro rang o day de
    // tranh loi "cannot find binary" (thong bao loi that tu geckodriver:
    // "Expected browser binary location, but unable to find binary in
    // default location, no 'moz:firefoxOptions.binary' capability
    // provided...").
    //
    // timeoutSeconds mac dinh 30 (khong phai 15 nhu ban dau) vi navigate
    // phai cho ca trang (kem JS) tai xong, lau hon nhieu so voi 1 request
    // API thong thuong nhu WebSearchTool. Rieng buoc KET NOI toi driver
    // (khong phai cho load trang) luon fail nhanh trong ~5s du timeoutSeconds
    // dat bao nhieu - xem CURLOPT_CONNECTTIMEOUT trong .cpp.
    //
    // maxTextChars: gioi han do dai van ban read_text() tra ve (mac dinh
    // 8000 ky tu). Trang that co the co vai chuc nghin ky tu innerText -
    // khong gioi han se de lam vo context cua LLM. textPreviewChars (nho
    // hon, mac dinh 1500) la doan preview NGAN hon nua ma navigate tra kem,
    // rieng biet voi gioi han nay.
    //
    // browser: BrowserType::Chrome (mac dinh, giu nguyen hanh vi cu) hoac
    // BrowserType::Firefox. Doi sang Firefox thi driver_url mac dinh
    // "...:9515" se SAI cong - truyen ro "http://127.0.0.1:4444" (cong mac
    // dinh cua geckodriver).
    explicit WebBrowserTool(std::string driver_url = "http://127.0.0.1:9515",
                             std::string browserBinaryPath = "",
                             int timeoutSeconds = 30,
                             int textPreviewChars = 1500,
                             int maxTextChars = 8000,
                             BrowserType browser = BrowserType::Chrome);

    // Can destructor rieng (khong the "= default" nhu Tool) vi phai dong
    // session trinh duyet that neu con dang mo - tranh leak process Chrome
    // moi lan agent chay xong.
    ~WebBrowserTool() override;

    // Khong cho copy: 1 session chi nen do 1 instance quan ly (2 ban copy se
    // cung tro toi 1 session_id_ roi cung goi DELETE 2 lan cho cung 1 id).
    WebBrowserTool(const WebBrowserTool&) = delete;
    WebBrowserTool& operator=(const WebBrowserTool&) = delete;

    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;

private:
    std::string driver_url_;
    std::string browser_binary_path_;
    int timeout_seconds_;
    int text_preview_chars_;
    int max_text_chars_;
    BrowserType browser_type_;

    // Tuong ung field "driver_session_id" trong class diagram cua nhom - o
    // day dat ten co "_" cuoi theo dung convention cac file khac trong repo
    // (vd api_key_ trong WebSearchTool). Rong ("") nghia la chua co session.
    std::string session_id_;

    // Ket qua 1 lan goi HTTP toi chromedriver - cung hinh dang HttpResult
    // nhu WebSearchTool (connected/status_code/body) de nguoi doc code sau
    // nhan ra ngay day la cung 1 pattern, chi khac noi goi toi va cach dung.
    struct HttpResult {
        bool connected = false;
        long status_code = 0;
        std::string body;
    };

    // Goi HTTP toi {driver_url_}{path} bang libcurl. Khac WebSearchTool o
    // cho WebDriver can nhieu method hon (POST de tao session/navigate/
    // chay script, DELETE de dong session) nen gom chung 1 ham nhan method
    // thay vi "httpPostJson" rieng nhu WebSearchTool chi can POST.
    HttpResult httpRequest(const std::string& method, const std::string& path,
                            const std::string& json_body) const;

    // Tao session moi qua POST /session neu session_id_ dang rong. Goi o
    // dau moi action can browser. Tra ve thong bao loi (neu that bai) de
    // execute() tra thang ve cho LLM, hoac nullopt neu thanh cong / da co
    // session tu truoc.
    std::optional<std::string> ensureSession();

    // Dong session qua DELETE /session/{id} neu dang mo, roi xoa
    // session_id_. Goi tu destructor - khong throw, khong quan tam ket qua
    // (best-effort cleanup, giong tinh than "don dep khi thoat" chu khong
    // phai thao tac nghiep vu can bao loi ro rang).
    void closeSession() noexcept;

    // Dich loi WebDriver: response loi co dang {"value":{"error":"...",
    // "message":"..."}} - khac han "detail.error" cua Tavily nen can ham
    // rieng, khong dung chung describeHttpError cua WebSearchTool duoc.
    std::string describeWebDriverError(long status_code, const std::string& body) const;

    std::optional<std::string> handleNavigate(const std::string& url);
    std::optional<std::string> handleReadText();
};