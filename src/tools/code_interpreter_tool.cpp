#include "code_interpreter_tool.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
    #define POPEN _popen
    #define PCLOSE _pclose
#else
    #define POPEN popen
    #define PCLOSE pclose
    #include <sys/wait.h>
#endif

#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

#ifndef _WIN32
// Escape 1 chuoi de dua an toan vao trong shell command (POSIX single-quote
// escaping): bao chuoi trong dau nhay don '...'; ben trong dau nhay don,
// shell KHONG dien giai bat ky ky tu dac biet nao (khong $, khong `, khong
// \) - ngoai tru chinh dau nhay don. Moi dau nhay don trong chuoi goc duoc
// thay bang '\'' (dong quote hien tai, chen 1 dau nhay da duoc escape bang
// backslash o NGOAI quote, roi mo quote lai). Day la ky thuat chuan, an
// toan cho moi ky tu input, dung de tranh loi da phat hien: workspace hoac
// filePath chua dau " se pha vo cach quote cu (\"...\"), cho phep chen them
// lenh shell tuy y (command injection).
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}
#endif

// GNU coreutils `timeout` coi thoi luong 0 la "TAT han timeout" (chay
// khong gioi han thoi gian) - dung NGUOC lai muc dich cua tham so nay.
// Ep toi thieu 1 giay de khong bao gio vo tinh tat het bao ve timeout chi
// vi ai do quen truyen gia tri hoac truyen 0/am.
int clampTimeoutSeconds(int seconds) {
    return seconds > 0 ? seconds : 1;
}

// Gia tri <= 0 duoc coi la "khong ap dung gioi han bo nho" mot cach co
// chu y va nhat quan (thay vi phu thuoc tinh co vao viec `memory_limit_kb_ > 0`
// duoc kiem tra dung cho o noi khac).
long clampMemoryLimitKb(long kb) {
    return kb > 0 ? kb : 0;
}

} // namespace

CodeInterpreterTool::CodeInterpreterTool(std::function<std::string()> pathProvider,
                                          int timeoutSeconds,
                                          long memoryLimitKb,
                                          std::string interpreter,
                                          std::string fileExtension)
    : path_provider_(std::move(pathProvider)),
      timeout_seconds_(clampTimeoutSeconds(timeoutSeconds)),
      memory_limit_kb_(clampMemoryLimitKb(memoryLimitKb)),
      interpreter_(std::move(interpreter)),
      file_extension_(std::move(fileExtension)) {}

std::string CodeInterpreterTool::getName() const { return "code_interpreter"; }

std::string CodeInterpreterTool::getDescription() const {
    std::ostringstream desc;
    desc << "Cong cu chay code " << interpreter_ << " thuc su (co vong lap, "
            "bien, if/else, import thu vien) - khac voi 'exec' (chi 1 dong "
            "lenh shell) va 'calculator' (chi 1 bieu thuc). Dung khi can "
            "logic nhieu buoc de tinh toan hoac xu ly du lieu. "
            "QUAN TRONG: moi lan goi la MOT chuong trinh doc lap (stateless) "
            "- KHONG giu bien/import tu lan goi truoc, phai tu khai bao lai "
            "tu dau moi lan. Code se bi huy sau "
         << timeout_seconds_ << " giay neu chay qua lau. "
            "Tham so dau vao (args) phai la JSON hop le: "
            "{\"code\": \"code can chay\"}";
    return desc.str();
}

std::string CodeInterpreterTool::writeCodeToTempFile(const std::string& workspace,
                                                      const std::string& code) const {
    fs::path dir = workspace.empty() ? fs::current_path() : fs::path(workspace);

    std::error_code ec;
    fs::create_directories(dir, ec); // khong throw neu da ton tai san

    // Ten file random de tranh dung do giua nhieu lan goi lien tiep.
    std::mt19937_64 rng(
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string filename = "snippet_" + std::to_string(rng()) + file_extension_;
    fs::path filePath = dir / filename;

    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Khong the tao file tam: " + filePath.string());
    }
    out << code;
    out.close();

    return filePath.string();
}

std::optional<std::string> CodeInterpreterTool::runInterpreter(
    const std::string& filePath, const std::string& workspace) const {
    std::ostringstream wrapped;

#ifdef _WIN32
    if (!workspace.empty()) {
        wrapped << "cd /d \"" << workspace << "\" && ";
    }
    // Windows: khong co ulimit/timeout builtin tuong duong - chay truc
    // tiep, best-effort. GHI CHU PHAM VI: cmd.exe co quy tac escape khac
    // POSIX shell (khong dung duoc shellQuote() ben duoi); nhanh Windows
    // nay van con lo hong tuong tu neu workspace chua ky tu dac biet cua
    // cmd.exe. Du an nay huong toi Linux la chinh (theo yeu cau de bai va
    // moi truong Arch Linux cua nhom), nen minh chua vi so day - can luu y
    // neu sau nay thuc su chay tren Windows.
    wrapped << interpreter_ << " \"" << filePath << "\" 2>&1";
#else
    if (!workspace.empty()) {
        wrapped << "cd " << shellQuote(workspace) << " && ";
    }
    if (memory_limit_kb_ > 0) {
        // ulimit la shell builtin, ap dung cho chinh shell nay va moi
        // tien trinh con duoc exec sau do (interpreter_) se ke thua gioi
        // han. Redirect loi ulimit (vd he thong khong cho phep) de khong
        // lam vo output.
        wrapped << "ulimit -v " << memory_limit_kb_ << " 2>/dev/null; ";
    }
    wrapped << "timeout " << timeout_seconds_ << "s "
            << interpreter_ << " " << shellQuote(filePath) << " 2>&1";
#endif

    // Gioi han tong so byte GIU LAI trong bo nho C++ - khong lien quan gi
    // toi ulimit/-v (cai do gioi han bo nho cua TIEN TRINH CON, khong gioi
    // han duoc viec chinh chuong trinh agent nay tu doc/tich luy string
    // qua lon). Da phat hien qua test: code in lien tuc trong vai giay co
    // the tao ra hang tram MB truoc khi bi `timeout` kill, va toan bo so
    // do bi giu trong RAM cua agent neu khong gioi han o day.
    constexpr size_t kMaxOutputBytes = 1'000'000; // ~1MB, du cho output hop ly

    std::array<char, 4096> buffer{};
    std::string result;
    bool truncated = false;

    FILE* raw_pipe = POPEN(wrapped.str().c_str(), "r");
    if (!raw_pipe) {
        return std::nullopt;
    }

    while (fgets(buffer.data(), buffer.size(), raw_pipe) != nullptr) {
        if (result.size() < kMaxOutputBytes) {
            result += buffer.data();
        } else {
            truncated = true;
            // Co y KHONG ngung vong lap o day - van tiep tuc doc (chi
            // khong luu) cho toi khi tien trinh con ket thuc/bi `timeout`
            // kill. Neu dung doc som, pipe se day len va tien trinh con bi
            // block khi ghi them - PCLOSE() ben duoi se cho vo han (deadlock)
            // vi tien trinh con khong bao gio thoat duoc.
        }
    }

    int rc = PCLOSE(raw_pipe);

    if (truncated) {
        result += "\n[CodeInterpreterTool] Output vuot " +
                  std::to_string(kMaxOutputBytes / 1000) +
                  " KB, phan sau da bi cat bot.";
    }

#ifndef _WIN32
    // GNU `timeout` thoat voi ma 124 rieng khi no phai tu giet tien trinh
    // con vi qua han - phan biet voi "chay xong that su khong co output"
    // de tra loi ro rang hon cho LLM (thay vi de no doan mo).
    if (WIFEXITED(rc) && WEXITSTATUS(rc) == 124) {
        result += "\n[CodeInterpreterTool] Code bi huy vi vuot qua timeout " +
                  std::to_string(timeout_seconds_) + " giay.";
    }
#else
    (void)rc;
#endif

    return result;
}

std::optional<std::string> CodeInterpreterTool::execute(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } 
    // C++26: Dùng '_' thay vì 'e' để khai báo biến vô danh
    catch (const json::parse_error& _) { 
        // Ta không cần in chi tiết lỗi của nlohmann, chỉ cần báo cho LLM
        return "Loi parse JSON dau vao: ";
    }

    if (!j.contains("code") || !j["code"].is_string()) {
        return std::string("Loi: JSON args thieu truong 'code' kieu string.");
    }

    std::string code;
    try {
        code = j["code"].get<std::string>();
    } catch (const json::type_error& e) {
        return std::string("Loi kieu du lieu JSON: ") + e.what();
    }

    if (code.empty()) {
        return std::string("Loi: 'code' rong.");
    }

    std::string workspace = path_provider_ ? path_provider_() : "";

    std::string filePath;
    try {
        filePath = writeCodeToTempFile(workspace, code);
    } catch (const std::exception& e) {
        return std::string("Loi ghi file tam: ") + e.what();
    }

    std::optional<std::string> output;
    try {
        output = runInterpreter(filePath, workspace);
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove(filePath, ec);
        return std::string("Loi thuc thi code: ") + e.what();
    }

    std::error_code ec;
    fs::remove(filePath, ec); // don file tam - khong giu lai sau khi chay xong

    if (!output.has_value()) {
        return std::string("Loi: khong the khoi chay process (popen).");
    }
    if (output->empty()) {
        return std::string("Code da chay xong (khong co output tra ve).");
    }
    return output;
}
