#pragma once
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

// ════════════════════════════════════════════════════════════════
//  Abstract Environment (Strategy)
//  Tool KHÔNG còn tự đụng vào OS/filesystem — Tool chỉ parse args +
//  áp policy riêng (chặn lệnh nguy hiểm, giới hạn kích thước...) rồi
//  ủy quyền hết phần "thực thi trên hệ thống" cho Environment.
//  Nhờ vậy Native vs Sandbox có thể chạy khác nhau (vd sau này
//  SandboxEnvironment dùng docker exec) mà Tool không cần biết/sửa gì.
// ════════════════════════════════════════════════════════════════
class Environment {
public:
    virtual ~Environment() = default;

    virtual void setup()    = 0;   // Gọi trước khi agent bắt đầu chạy
    virtual void teardown() = 0;   // Gọi sau khi eval xong để dọn dẹp
    virtual const std::string& getWorkspace() const = 0;
    virtual bool supportsGui() const { return false; }

    // Chạy lệnh shell trong workspace. Đây là điểm KHÁC BIỆT thật sự
    // giữa các Environment nên để pure virtual.
    // nullopt = không khởi chạy được process (lỗi hạ tầng).
    virtual std::optional<std::string> execute(const std::string& command,
                                                 int timeoutSeconds) = 0;

    // Thao tác file: Native/Sandbox hiện tại đều làm việc trực tiếp
    // trên std::filesystem, chỉ khác root -> cho impl mặc định dùng
    // chung, subclass override nếu sau này cần hành vi khác.
    virtual std::optional<std::string> readFile(const std::string& relativePath) const;
    virtual std::optional<std::string> writeFile(const std::string& relativePath,
                                                   const std::string& content,
                                                   bool append);
    virtual std::optional<std::string> listDir(const std::string& relativePath) const;
    virtual std::optional<std::string> deleteEntry(const std::string& relativePath);
    virtual std::optional<std::string> makeDir(const std::string& relativePath);

protected:
    // Chạy nguyên văn 1 command string qua popen, đọc toàn bộ stdout+stderr.
    // KHÔNG tự thêm cd/timeout — mỗi Environment tự build command string
    // phù hợp (native: cd + timeout; sandbox: docker exec ...).
    std::optional<std::string> popenCapture(const std::string& fullCommand) const;
        //[FIX - BUG]
    // Bọc `command` trong dấu nháy đơn để nhét AN TOÀN vào 1 lớp `sh -c '...'`,
    // escape các dấu nháy đơn có sẵn bên trong theo kiểu POSIX chuẩn:
    // ' -> '\''  (đóng quote, echo 1 quote literal, mở lại quote).
    // Dùng chung cho Native + Sandbox để đảm bảo `timeout` (hoặc lớp
    // sh -c ngoài cùng trong Sandbox) luôn bọc quanh TOÀN BỘ command,
    // kể cả khi command nối chuỗi bằng &&, ||, ; hoặc |. Có thể gọi
    // LỒNG NHAU (shellQuote 2 lần) để đưa 1 chuỗi đã có sh -c vào 1 lớp
    // sh -c ngoài khác (vd docker exec ... sh -c '...').
    static std::string shellQuote(const std::string& command);

    // Ghép relativePath với workspace root, chuẩn hoá (weakly_canonical)
    // và đảm bảo kết quả vẫn nằm TRONG root -> chống path traversal
    // (../, path tuyệt đối, symlink thoát ra ngoài workspace).
    std::optional<std::filesystem::path> resolveSafePath(
        const std::string& relativePath) const;
};

class NativeEnvironment : public Environment {
    std::string work_dir_;
public:
    explicit NativeEnvironment(const std::string& dir = "workspace/native_workspace");
    void setup()    override;
    void teardown() override;
    const std::string& getWorkspace() const override { return work_dir_; }
    bool supportsGui() const override { return true; }
    std::optional<std::string> execute(const std::string& command,
                                         int timeoutSeconds) override;
};

class SandboxEnvironment : public Environment {
    std::string base_dir_;
    std::string prefix_;
    std::string sandbox_dir_;    // thư mục host, bind-mount vào container
    std::string image_;          // docker image dùng làm sandbox
    std::string container_id_;
    bool        active_ = false;
public:
    explicit SandboxEnvironment(const std::string& base_dir = "workspace/sandbox",
                            const std::string& prefix   = "sandbox_",
                            const std::string& image    = "alpine:3.20");
    void setup()    override;
    void teardown() override;
    const std::string& getWorkspace() const override { return sandbox_dir_; }
    std::optional<std::string> execute(const std::string& command,
                                         int timeoutSeconds) override;
};