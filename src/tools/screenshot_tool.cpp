#include "screenshot_tool.h"
#include <dbus/dbus.h>
#include <chrono>
#include <ctime>
#include <format>
#include <thread>
#include <random>
#include <sstream>

ScreenshotTool::ScreenshotTool(WorkspaceProvider workspace_provider)
    : workspace_provider_(std::move(workspace_provider)) {
    // Không tạo thư mục ở đây: workspace có thể chưa sẵn sàng lúc constructor
    // chạy (Environment::setup() có thể được gọi sau khi tool đã đăng ký).
    // Thư mục sẽ được tạo lazy ngay trước khi dùng, trong generateOutputPath().
}

std::string ScreenshotTool::getName() const {
    return "capture_screenshot";
}

std::string ScreenshotTool::getDescription() const {
    return "Chup toan bo man hinh desktop hien tai (qua xdg-desktop-portal) "
           "va luu ra file PNG. Lan dau goi co the hien dialog xin quyen. "
           "Tra ve duong dan file anh vua chup.";
}

std::filesystem::path ScreenshotTool::generateOutputPath() const {
    std::filesystem::path save_dir = workspace_provider_() / "screenshots";
    std::filesystem::create_directories(save_dir);

    // Format: shot_<nam><thang><ngay>_<gio><phut><giay>.png
    // Vi du: shot_20260723_143059.png
    // Dung yyyyMMdd truoc de sort theo ten file ~= sort theo thoi gian.
    std::time_t now_c = std::time(nullptr);
    std::tm local_tm{};
    localtime_r(&now_c, &local_tm); // ham POSIX, thread-safe (Linux/macOS)

    char buf[32];
    std::strftime(buf, sizeof(buf), "shot_%Y%m%d_%H%M%S.png", &local_tm);

    std::filesystem::path candidate = save_dir / buf;

    // Neu 2 lan chup roi vao cung 1 giay (trung ten), them hau to _1, _2...
    // de khong ghi de anh cu.
    if (std::filesystem::exists(candidate)) {
        int suffix = 1;
        std::filesystem::path alt;
        do {
            char buf_alt[40];
            std::strftime(buf_alt, sizeof(buf_alt), "shot_%Y%m%d_%H%M%S", &local_tm);
            alt = save_dir / (std::string(buf_alt) + "_" + std::to_string(suffix) + ".png");
            ++suffix;
        } while (std::filesystem::exists(alt));
        return alt;
    }

    return candidate;
}

// Sinh token ngẫu nhiên cho request/handle theo yêu cầu spec portal
static std::string randomToken() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 15);
    std::ostringstream oss;
    oss << "claude_agent_";
    for (int i = 0; i < 8; ++i) {
        oss << std::hex << dist(gen);
    }
    return oss.str();
}

bool ScreenshotTool::callPortalScreenshot(std::string& uri_out, std::string& error_out) const {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        error_out = err.message;
        dbus_error_free(&err);
        return false;
    }

    // Lấy unique name của mình để build đường dẫn handle theo spec portal
    const char* unique_name = dbus_bus_get_unique_name(conn);
    std::string sender = unique_name ? unique_name + 1 : ""; // bỏ dấu ':' đầu
    for (auto& c : sender) if (c == '.') c = '_';

    std::string handle_token = randomToken();
    std::string expected_handle =
        "/org/freedesktop/portal/desktop/request/" + sender + "/" + handle_token;

    // Subscribe signal Response trên request object trước khi gọi Screenshot
    std::string match_rule =
        "type='signal',interface='org.freedesktop.portal.Request',"
        "member='Response',path='" + expected_handle + "'";
    dbus_bus_add_match(conn, match_rule.c_str(), &err);
    if (dbus_error_is_set(&err)) {
        error_out = err.message;
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_flush(conn);

    // Build message gọi Screenshot(parent_window="", options={"handle_token": token})
    DBusMessage* msg = dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Screenshot",
        "Screenshot");

    const char* parent_window = "";
    DBusMessageIter iter, options_iter, entry_iter, variant_iter;
    dbus_message_iter_init_append(msg, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_window);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &options_iter);

    const char* key = "handle_token";
    const char* token_cstr = handle_token.c_str();
    dbus_message_iter_open_container(&options_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry_iter);
    dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
    dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &token_cstr);
    dbus_message_iter_close_container(&entry_iter, &variant_iter);
    dbus_message_iter_close_container(&options_iter, &entry_iter);

    dbus_message_iter_close_container(&iter, &options_iter);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 5000, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err) || !reply) {
        error_out = dbus_error_is_set(&err) ? err.message : "Khong nhan duoc reply goi Screenshot";
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        return false;
    }

    const char* returned_handle = nullptr;
    dbus_message_get_args(reply, &err, DBUS_TYPE_OBJECT_PATH, &returned_handle, DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    if (dbus_error_is_set(&err)) {
        error_out = err.message;
        dbus_error_free(&err);
        return false;
    }

    // ---- Đợi signal Response (user bấm Allow/Deny) ----
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    bool got_response = false;
    uint32_t response_code = 1; // 1 = cancelled mặc định

    while (std::chrono::steady_clock::now() < deadline) {
        dbus_connection_read_write(conn, 200);
        DBusMessage* signal_msg = dbus_connection_pop_message(conn);
        if (!signal_msg) continue;

        if (dbus_message_is_signal(signal_msg, "org.freedesktop.portal.Request", "Response")) {
            DBusMessageIter sig_iter;
            dbus_message_iter_init(signal_msg, &sig_iter);
            dbus_message_iter_get_basic(&sig_iter, &response_code);

            if (response_code == 0) { // 0 = success
                dbus_message_iter_next(&sig_iter); // vào dict a{sv}
                DBusMessageIter dict_iter;
                dbus_message_iter_recurse(&sig_iter, &dict_iter);
                while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter entry, variant;
                    dbus_message_iter_recurse(&dict_iter, &entry);
                    const char* k = nullptr;
                    dbus_message_iter_get_basic(&entry, &k);
                    dbus_message_iter_next(&entry);
                    dbus_message_iter_recurse(&entry, &variant);
                    if (k && std::string(k) == "uri") {
                        const char* uri = nullptr;
                        dbus_message_iter_get_basic(&variant, &uri);
                        if (uri) uri_out = uri;
                    }
                    dbus_message_iter_next(&dict_iter);
                }
            }
            got_response = true;
        }
        dbus_message_unref(signal_msg);
        if (got_response) break;
    }

    if (!got_response) {
        error_out = "Timeout: user khong phan hoi dialog xin quyen trong 60s";
        return false;
    }
    if (response_code != 0) {
        error_out = "User tu choi hoac huy quyen chup man hinh (code=" + std::to_string(response_code) + ")";
        return false;
    }
    if (uri_out.empty()) {
        error_out = "Response thanh cong nhung khong co uri anh";
        return false;
    }

    return true;
}

std::optional<std::string> ScreenshotTool::execute(const std::string& /*args*/) {
    std::string uri, err;
    if (!callPortalScreenshot(uri, err)) {
        return std::nullopt;
    }

    // uri dạng "file:///tmp/xxxx.png" -> chuyển thành path thường
    std::string path_str = uri;
    const std::string prefix = "file://";
    if (path_str.rfind(prefix, 0) == 0) {
        path_str = path_str.substr(prefix.size());
    }

    if (!std::filesystem::exists(path_str)) {
        return std::nullopt;
    }

    // Copy vào workspace hiện tại để thống nhất chỗ lưu
    std::filesystem::path dest = generateOutputPath();
    std::error_code ec;
    std::filesystem::copy_file(path_str, dest, ec);
    if (ec) {
        return path_str; // fallback: trả path gốc nếu copy lỗi
    }

    return dest.string();
}