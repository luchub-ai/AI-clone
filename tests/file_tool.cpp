#include <iostream>
#include <string>
#include <filesystem>
#include "src/tools/file_tool.h"

namespace fs = std::filesystem;

// Hàm tiện ích để in kết quả test dễ nhìn (giống style tests/memory_tool.cpp)
void printTestResult(const std::string& test_name, const std::optional<std::string>& result) {
    std::cout << "--------------------------------------------------\n";
    std::cout << "[TEST] " << test_name << "\n";
    if (result.has_value()) {
        std::cout << ">> KET QUA:\n" << result.value() << "\n";
    } else {
        std::cout << ">> KET QUA: [That bai / nullopt]\n";
    }
    std::cout << "--------------------------------------------------\n\n";
}

int main() {
    std::cout << "BAT DAU TEST FILE TOOL...\n\n";

    // Tao 1 "phong" (workspace) rieng cho test, giong nhu NativeEnvironment
    // se lam trong chuong trinh that.
    fs::path workspace = fs::current_path() / "test_file_tool_workspace";
    fs::create_directories(workspace);

    // path_provider_ o day chi la 1 lambda tra ve string co dinh, vi test
    // don gian khong can Environment that.
    FileTool file_tool([workspace] { return workspace.string(); });

    std::cout << "Ten Tool: " << file_tool.getName() << "\n";
    std::cout << "Mo ta Tool:\n" << file_tool.getDescription() << "\n\n";

    // 1. Tao thu muc con
    printTestResult("Tao thu muc 'notes'",
        file_tool.execute(R"({"action": "mkdir", "path": "notes"})"));

    // 2. Ghi file moi (tu dong tao thu muc cha neu can)
    printTestResult("Ghi file 'notes/todo.txt'",
        file_tool.execute(R"({"action": "write", "path": "notes/todo.txt", "content": "Hoc OOP"})"));

    // 3. Noi them noi dung vao cuoi file
    printTestResult("Noi them vao 'notes/todo.txt'",
        file_tool.execute(R"({"action": "append", "path": "notes/todo.txt", "content": "\nLam bai tap"})"));

    // 4. Doc lai file
    printTestResult("Doc 'notes/todo.txt'",
        file_tool.execute(R"({"action": "read", "path": "notes/todo.txt"})"));

    // 5. Liet ke thu muc
    printTestResult("Liet ke 'notes'",
        file_tool.execute(R"({"action": "list", "path": "notes"})"));

    // 6. Doc file khong ton tai (mo phong loi binh thuong)
    printTestResult("Doc file khong ton tai",
        file_tool.execute(R"({"action": "read", "path": "notes/khong_ton_tai.txt"})"));

    // 7. THU THOAT RA NGOAI WORKSPACE bang '..'
    printTestResult("Chan path traversal ('../../../etc/passwd')",
        file_tool.execute(R"({"action": "read", "path": "../../../etc/passwd"})"));

    // 8. THU THOAT RA NGOAI WORKSPACE bang duong dan tuyet doi
    printTestResult("Chan path tuyet doi ('/etc/passwd')",
        file_tool.execute(R"({"action": "read", "path": "/etc/passwd"})"));

    // 9. JSON sai dinh dang (mo phong LLM tra loi sai)
    printTestResult("Xu ly loi JSON sai cu phap",
        file_tool.execute(R"({action: "read", path: "notes/todo.txt"})"));

    // 10. Action khong duoc ho tro
    printTestResult("Xu ly loi action khong hop le",
        file_tool.execute(R"({"action": "format_c_drive", "path": "notes"})"));

    // 11. Xoa file
    printTestResult("Xoa 'notes/todo.txt'",
        file_tool.execute(R"({"action": "delete", "path": "notes/todo.txt"})"));

    printTestResult("Liet ke 'notes' sau khi xoa",
        file_tool.execute(R"({"action": "list", "path": "notes"})"));

    // Don dep workspace test
    fs::remove_all(workspace);

    std::cout << "HOAN THANH TEST!\n";
    return 0;
}