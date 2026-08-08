#include "environment.h" // Tùy vào tên file header thực tế của bạn
#include "utils/env_loader.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

NativeEnvironment::NativeEnvironment(const std::string& dir) {
    // 1. Kéo đường dẫn về ngay tại thư mục Project hiện tại (Nơi bạn chạy mã)
    fs::path project_root = fs::current_path();
    work_dir_ = (project_root / dir).string();
}

void NativeEnvironment::setup() {
    if (!fs::exists(work_dir_)) {
        fs::create_directories(work_dir_);
    }

    EnvUtils::loadEnv(); //load trong file .env
    
    std::cout << "[NativeEnv] Workspace ready at project root: "
              << fs::absolute(work_dir_).string() << "\n";
}

void NativeEnvironment::teardown() {
    // 2. ĐÃ XÓA LỆNH 
    // fs::remove_all(work_dir_)
    // Chỉ cập nhật trạng thái và in log thông báo giữ lại file
    if (fs::exists(work_dir_)) {
        std::cout << "[NativeEnv] Teardown complete. : " << work_dir_ << "\n";
    }
}

std::optional<std::string> NativeEnvironment::execute(const std::string& command,
                                                         int timeoutSeconds) {
    std::ostringstream wrapped;
#ifdef _WIN32
    wrapped << "cd /d \"" << work_dir_ << "\" && " << command << " 2>&1";
#else
    //[FIX - BUG]
    // QUAN TRONG: khong duoc dua `command` (co the chua &&, ||, ;, |) truc
    // tiep sau `timeout Ns`, vi shell se tach no thanh nhieu lenh doc lap
    // va `timeout` chi bao boc dung LENH DAU TIEN truoc && -> cac lenh sau
    // (vd sleep) chay tu do, khong bao gio bi cat du het thoi gian.
    // Fix: boc CA CHUOI lenh vao 1 subshell `sh -c '...'` truoc, roi moi
    // dua nguyen ca subshell do cho `timeout` -> timeout se giam sat/kill
    // dung 1 process (subshell) chua toan bo chuoi lenh.
    wrapped << "cd \"" << work_dir_ << "\" && timeout " << timeoutSeconds
             << "s sh -c " << shellQuote(command) << " 2>&1";
#endif
    return popenCapture(wrapped.str());
}