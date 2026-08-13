#include "screenshot_tool.h"
#include <dbus/dbus.h>
#include <chrono>
#include <ctime>
#include <format>
#include <thread>
#include <random>
#include <sstream>

// Day la don vi bien dich (.cpp) DUY NHAT trong project include 3 file stb
// nay voi macro IMPLEMENTATION - dinh nghia phan than ham thuc te. Neu file
// .cpp khac cung define lai cac macro nay va include lai header, se bi loi
// "multiple definition" luc link. Chi can 1 noi duy nhat.
#define STB_IMAGE_IMPLEMENTATION
#include "../../utils/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../utils/stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../../utils/stb_image_resize2.h"

ScreenshotTool::ScreenshotTool(WorkspaceProvider workspace_provider,
                               std::optional<int> max_width)
    : workspace_provider_(std::move(workspace_provider)),
      max_width_(max_width) {
    // Khong tao thu muc o day: workspace co the chua san sang luc constructor
    // chay (Environment::setup() co the duoc goi sau khi tool da dang ky).
    // Thu muc se duoc tao lazy ngay truoc khi dung, trong generateOutputPath().
}

bool ScreenshotTool::resizeIfNeeded(const std::filesystem::path& path) {
    if (!max_width_.has_value()) {
        last_resize_ratio_ = 1.0;
        return true; // khong yeu cau resize -> giu nguyen, khong tinh la loi
    }

    int orig_w = 0, orig_h = 0, channels = 0;
    // "3" ep luon doc ra 3 channel (RGB) - bo alpha cho don gian, du cho
    // muc dich model "nhin" anh, khong can trong suot.
    unsigned char* pixels = stbi_load(path.string().c_str(), &orig_w, &orig_h,
                                       &channels, 3);
    if (!pixels) {
        // Doc anh loi: khong resize, giu file goc, khong lam crash ca tool.
        last_resize_ratio_ = 1.0;
        return false;
    }

    if (orig_w <= *max_width_) {
        // Anh da du nho, khong can resize.
        stbi_image_free(pixels);
        last_resize_ratio_ = 1.0;
        return true;
    }

    const int new_w = *max_width_;
    // Giu nguyen ti le khung hinh (aspect ratio).
    const int new_h = static_cast<int>(
        std::lround(static_cast<double>(orig_h) * new_w / orig_w));

    std::vector<unsigned char> resized_pixels(
        static_cast<size_t>(new_w) * new_h * 3);

    unsigned char* result = stbir_resize_uint8_linear(
        pixels, orig_w, orig_h, 0,
        resized_pixels.data(), new_w, new_h, 0,
        STBIR_RGB);

    stbi_image_free(pixels);

    if (!result) {
        last_resize_ratio_ = 1.0;
        return false;
    }

    const int write_ok = stbi_write_png(
        path.string().c_str(), new_w, new_h, 3,
        resized_pixels.data(), new_w * 3);

    if (!write_ok) {
        last_resize_ratio_ = 1.0;
        return false;
    }

    // Ti le de InputTool nhan them vao scale_x/scale_y: toa do model tra ve
    // duoc tinh tren anh MOI (nho hon), nen phai nhan nguoc lai ti le nay
    // de ra dung toa do pixel tren man hinh that.
    last_resize_ratio_ = static_cast<double>(orig_w) / new_w;
    return true;
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

    std::time_t now_c = std::time(nullptr);
    std::tm local_tm{};
    localtime_r(&now_c, &local_tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "shot_%Y%m%d_%H%M%S.png", &local_tm);

    std::filesystem::path candidate = save_dir / buf;

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

// RAII guard: dam bao dbus_bus_remove_match luon duoc goi khi ham
// callPortalScreenshot ket thuc, du la return som vi loi hay thanh cong.
// Neu khong co guard nay, moi lan goi Screenshot() them 1 match rule
// tren session bus ma khong bao gio duoc go, tich luy dan trong 1
// GUIAgentLoop chay lien tuc nhieu vong.
namespace {
class DBusMatchGuard {
public:
    DBusMatchGuard(DBusConnection* conn, std::string rule)
        : conn_(conn), rule_(std::move(rule)) {}
    ~DBusMatchGuard() {
        if (conn_) {
            DBusError err;
            dbus_error_init(&err);
            dbus_bus_remove_match(conn_, rule_.c_str(), &err);
            if (dbus_error_is_set(&err)) dbus_error_free(&err);
        }
    }
    DBusMatchGuard(const DBusMatchGuard&) = delete;
    DBusMatchGuard& operator=(const DBusMatchGuard&) = delete;

private:
    DBusConnection* conn_;
    std::string rule_;
};
} // namespace

bool ScreenshotTool::callPortalScreenshot(std::string& uri_out, std::string& error_out) const {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        error_out = err.message;
        dbus_error_free(&err);
        return false;
    }

    const char* unique_name = dbus_bus_get_unique_name(conn);
    std::string sender = unique_name ? unique_name + 1 : "";
    for (auto& c : sender) if (c == '.') c = '_';

    std::string handle_token = randomToken();
    std::string expected_handle =
        "/org/freedesktop/portal/desktop/request/" + sender + "/" + handle_token;

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

    // Tu day tro di neu return som (loi hay thanh cong) thi match rule
    // van luon duoc go nho guard nay.
    DBusMatchGuard match_guard(conn, match_rule);

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

    // Timeout ngan hon (10s) cho call ban dau: day chi la B1 (gui yeu cau,
    // portal tra ve object path ngay), khong phai luc doi user bam Allow -
    // buoc do nam o vong doi signal Response ben duoi.
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 10000, &err);
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

    // ---- Doi signal Response ----
    // 60s cho lan dau (user co the mat vai giay de bam Allow). Neu quyen da
    // duoc "Remember" tu lan truoc, GNOME tra Response gan nhu ngay lap tuc
    // nen thoi gian cho thuc te trong GUIAgentLoop se rat ngan.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    bool got_response = false;
    uint32_t response_code = 1; // 1 = cancelled mac dinh

    while (std::chrono::steady_clock::now() < deadline) {
        dbus_connection_read_write(conn, 200);
        DBusMessage* signal_msg = dbus_connection_pop_message(conn);
        if (!signal_msg) continue;

        // Chi xu ly dung signal cua request nay (phong truong hop connection
        // dung chung voi phan khac cua app nhan duoc message khac chen vao).
        const bool is_ours =
            dbus_message_is_signal(signal_msg, "org.freedesktop.portal.Request", "Response") &&
            dbus_message_get_path(signal_msg) != nullptr &&
            expected_handle == dbus_message_get_path(signal_msg);

        if (!is_ours) {
            dbus_message_unref(signal_msg);
            continue;
        }

        DBusMessageIter sig_iter;
        dbus_message_iter_init(signal_msg, &sig_iter);
        dbus_message_iter_get_basic(&sig_iter, &response_code);

        if (response_code == 0) { // 0 = success
            dbus_message_iter_next(&sig_iter);
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
        dbus_message_unref(signal_msg);
        break;
    }

    if (!got_response) {
        error_out = "Timeout: khong nhan duoc phan hoi tu portal trong 60s "
                     "(user chua bam Allow lan dau, hoac quyen da bi thu hoi)";
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

    std::string path_str = uri;
    const std::string prefix = "file://";
    if (path_str.rfind(prefix, 0) == 0) {
        path_str = path_str.substr(prefix.size());
    }

    if (!std::filesystem::exists(path_str)) {
        return std::nullopt;
    }

    std::filesystem::path dest = generateOutputPath();
    std::error_code ec;
    std::filesystem::copy_file(path_str, dest, ec);
    if (ec) {
        return path_str;
    }

    // Resize (neu can) NGAY TREN file da copy vao workspace - khong dung
    // gi den anh goc trong /tmp cua portal. Loi resize khong lam fail ca
    // tool: cu tra ve anh (kich thuoc goc) con hon la mat luon screenshot.
    resizeIfNeeded(dest);

    return dest.string();
}