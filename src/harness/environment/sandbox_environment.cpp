#include "environment.h" // Tùy vào tên file header thực tế của bạn
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "utils/env_loader.h" // nhắc t tiêm thằng này vô để chạy cái thằng web search tool

namespace fs = std::filesystem;

SandboxEnvironment::SandboxEnvironment(const std::string& base_dir, const std::string& prefix,
                                         const std::string& image)
            : base_dir_(base_dir), prefix_(prefix), image_(image) {}

void SandboxEnvironment::setup() {
    EnvUtils::loadEnv();
    fs::path base_dir = fs::current_path() / base_dir_;
    std::error_code ec;
    fs::create_directories(base_dir, ec);
    if (ec) throw std::runtime_error("[SandboxEnv]: khong tao duoc base_dir " + base_dir.string() + ": " + ec.message());

    std::string tmpl = (base_dir / (prefix_ + "XXXXXX")).string();

    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* dir = mkdtemp(buf.data());
    if (!dir) throw std::runtime_error("[SandboxEnv]: mkdtemp that bai");
    sandbox_dir_ = dir;

    std::cout << "[SandboxEnv] Isolated sandbox created in project: " << sandbox_dir_ << "\n";
    // Chay container nen, mount workspace vao /workspace, giu song bang
    // sleep infinity de co the docker exec nhieu lan sau nay.
    std::ostringstream cmd;
    cmd << "docker run -d --rm -v \"" << sandbox_dir_ << "\":/workspace "
        << "-w /workspace " << image_ << " sleep infinity";

    auto id = popenCapture(cmd.str());
    if (!id || id->empty())
        throw std::runtime_error(
            "SandboxEnvironment: khong khoi chay duoc Docker container "
            "(kiem tra Docker daemon co dang chay khong).");

    container_id_ = *id;
    while (!container_id_.empty() &&
           (container_id_.back() == '\n' || container_id_.back() == '\r'))
        container_id_.pop_back();

    active_ = true;
    
    if (!dir) throw std::runtime_error("[SandboxEnv]: mkdtemp that bai tai '" + tmpl + "': " + std::strerror(errno));
}

void SandboxEnvironment::teardown() {
    if (!active_) return;
    if (!container_id_.empty())
        popenCapture("docker stop " + container_id_ + " >/dev/null 2>&1");
    std::error_code ec;
    std::filesystem::remove_all(sandbox_dir_, ec);
    active_ = false;
    container_id_.clear();
}

std::optional<std::string> SandboxEnvironment::execute(const std::string& command,
                                                          int timeoutSeconds) {
    if (container_id_.empty()) return std::nullopt;

    //[FIX - BUG]
    // Cung 1 loi nhu NativeEnvironment: neu chi nhet thang `command` (co the
    // chua &&, ||, ;, |) ngay sau `timeout Ns` ben trong 1 lop sh -c '...'
    // duy nhat, shell van tach no thanh nhieu lenh doc lap va `timeout` chi
    // bao boc lenh DAU TIEN -> cac lenh sau (vd sleep) van chay het gio,
    // khong bi cat.
    //
    // Fix: LONG 2 LOP sh -c:
    //   - Lop TRONG: boc nguyen ca chuoi `command` vao 1 subshell rieng
    //     (`sh -c '<command>'`) truoc khi dua cho `timeout` -> timeout
    //     giam sat dung 1 process con chua toan bo chuoi lenh.
    //   - Lop NGOAI: giu nguyen ha tang san co (`docker exec <id> sh -c
    //     '...'`) de goi duoc vao trong container, chi khac la be trong no
    //     gio la ca cum "timeout Ns sh -c '<command>'" nen phai escape 2
    //     lan (shellQuote long nhau) cho dung dau nhay don.
    std::string inner = "timeout " + std::to_string(timeoutSeconds) +
                         "s sh -c " + shellQuote(command) + " 2>&1";

    std::ostringstream wrapped;
    wrapped << "docker exec " << container_id_ << " sh -c " << shellQuote(inner);
    return popenCapture(wrapped.str());
}