// tests/test_code_interpreter.cpp
//
// Bo test toan dien cho CodeInterpreterTool - bao phu tat ca nhanh code
// trong src/tools/code_interpreter_tool.cpp:
//   - Metadata (getName/getDescription)
//   - Parse args: JSON hop le, JSON sai, thieu field 'code', 'code' sai
//     kieu (khong phai string), 'code' rong
//   - Thuc thi binh thuong: vong lap, bien, import, ham, class nhieu dong
//   - Khong co output (khong print gi) vs co output
//   - Loi runtime cua chinh code Python (traceback), gop stdout+stderr
//   - Stateless giua 2 lan goi lien tiep (khong nho bien/import cu)
//   - Timeout (vong lap vo han bi `timeout` kill dung han, khong treo)
//   - Gioi han memory qua ulimit -v (memory_limit_kb > 0)
//   - memory_limit_kb = 0/am -> KHONG gioi han (clampMemoryLimitKb)
//   - timeout_seconds = 0/am -> tu dong ep ve 1s (clampTimeoutSeconds)
//   - Ghi/doc file that trong workspace (tich hop voi path_provider)
//   - Workspace la duong dan "nguy hiem" chua dau nhay don ' -> kiem tra
//     shellQuote() chong command injection khong lam vo lenh
//   - Code chua ky tu dac biet shell ($, `, ", ;, |) van chay dung vi code
//     duoc ghi ra FILE that (data) chu khong noi vao chuoi lenh
//   - Output vuot 1MB bi cat bot (kMaxOutputBytes) nhung khong deadlock
//   - Don dep file tam sau khi chay xong (khong con file snippet_*.py rac)
//
// Cach chay (Linux, da cai nlohmann-json3-dev):
//   g++ -std=c++23 -O1 tests/test_code_interpreter.cpp \
//       src/tools/code_interpreter_tool.cpp -I. -Isrc -o /tmp/test_ci
//   /tmp/test_ci
//
// LUU Y: KHONG dung -lnlohmann_json (thu vien header-only, khong co file
// .so/.a de link - xem giai thich trong hoi thoai).
//
// Trien khai: moi test la 1 ham tra ve bool, khong throw/abort ngay khi
// that bai (khac ban cu dung assert()) - de 1 case fail KHONG lam mat het
// ket qua cac case con lai. Cuoi cung in bang tong ket PASS/FAIL.

#include "src/tools/code_interpreter_tool.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// ── Tien ich dung chung ────────────────────────────────────────────
std::string args(const std::string& code) {
    json j;
    j["code"] = code;
    return j.dump();
}

struct TestCase {
    std::string name;
    std::function<bool()> run; // true = PASS
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

void addTest(const std::string& name, std::function<bool()> fn) {
    registry().push_back({name, std::move(fn)});
}

// check(...) : ghi log dieu kien fail cu the thay vi assert "cham" tai cho.
bool check(bool cond, const std::string& msg) {
    if (!cond) {
        std::cout << "    [FAIL condition] " << msg << "\n";
    }
    return cond;
}

// Dem so file "snippet_*" con sot lai trong 1 thu muc (kiem tra don dep).
int countSnippetFiles(const std::string& dir) {
    int count = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().filename().string().rfind("snippet_", 0) == 0) ++count;
    }
    return count;
}

} // namespace

// ════════════════════════════════════════════════════════════════
//  DANG KY TEST
// ════════════════════════════════════════════════════════════════

void registerAllTests() {
    const std::string workspace = "/tmp/ci_tool_test_workspace";

    // ---- 1. Metadata ------------------------------------------------
    addTest("Metadata: getName() = 'code_interpreter'", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        return check(tool.getName() == "code_interpreter", "ten tool sai: " + tool.getName());
    });

    addTest("Metadata: getDescription() chua ten interpreter + timeout", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 3, 262144, "python3");
        std::string d = tool.getDescription();
        bool ok = check(d.find("python3") != std::string::npos, "thieu ten interpreter trong description");
        ok &= check(d.find("3") != std::string::npos, "thieu so giay timeout trong description");
        return ok;
    });

    // ---- 2. Thuc thi binh thuong ------------------------------------
    addTest("Exec: vong lap + bien + print", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(args("total = 0\nfor i in range(1, 101):\n    total += i\nprint('sum:', total)"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("sum: 5050") != std::string::npos, "sai ket qua tong: " + *r);
    });

    addTest("Exec: ham + class nhieu dong (logic phuc tap)", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 3, 262144);
        std::string code =
            "class Counter:\n"
            "    def __init__(self):\n"
            "        self.n = 0\n"
            "    def inc(self):\n"
            "        self.n += 1\n"
            "        return self.n\n"
            "\n"
            "def fib(k):\n"
            "    a, b = 0, 1\n"
            "    for _ in range(k):\n"
            "        a, b = b, a + b\n"
            "    return a\n"
            "\n"
            "c = Counter()\n"
            "for _ in range(5):\n"
            "    c.inc()\n"
            "print('counter:', c.n)\n"
            "print('fib10:', fib(10))\n";
        auto r = tool.execute(args(code));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("counter: 5") != std::string::npos, "sai ket qua counter: " + *r)
            && check(r->find("fib10: 55") != std::string::npos, "sai ket qua fib: " + *r);
    });

    addTest("Exec: import thu vien chuan (math, json)", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 3, 262144);
        auto r = tool.execute(args(
            "import math, json\n"
            "print(round(math.sqrt(2), 4))\n"
            "print(json.dumps({'ok': True}))\n"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("1.4142") != std::string::npos, "sai ket qua sqrt: " + *r)
            && check(r->find("\"ok\": true") != std::string::npos, "sai ket qua json.dumps: " + *r);
    });

    // ---- 3. Khong co output vs co output -----------------------------
    addTest("Exec: code chay xong nhung KHONG print gi -> thong bao ro rang", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(args("x = 1 + 1"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("khong co output") != std::string::npos,
                     "phai bao 'khong co output' khi code khong print: " + *r);
    });

    // ---- 4. Loi runtime cua code Python -------------------------------
    addTest("Exec: loi runtime (ZeroDivisionError) tra ve traceback, khong crash", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(args("print(1/0)"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("ZeroDivisionError") != std::string::npos, "thieu ZeroDivisionError: " + *r);
    });

    addTest("Exec: loi cu phap (SyntaxError) van tra ve output ro rang", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(args("def broken(:\n    pass"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("SyntaxError") != std::string::npos, "thieu SyntaxError: " + *r);
    });

    addTest("Exec: stdout va stderr duoc gop chung (in truoc khi loi)", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(args("print('before-error')\nraise ValueError('boom')"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("before-error") != std::string::npos, "thieu stdout truoc loi: " + *r)
            && check(r->find("ValueError") != std::string::npos, "thieu stderr (traceback): " + *r);
    });

    // ---- 5. Parse args sai ---------------------------------------------
    addTest("Args: thieu truong 'code'", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(R"({"foo": "bar"})");
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("thieu truong") != std::string::npos, "thong bao loi khong dung: " + *r);
    });

    addTest("Args: JSON khong hop le (loi cu phap JSON)", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute("{not json}");
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("parse JSON") != std::string::npos, "thong bao loi khong dung: " + *r);
    });

    addTest("Args: 'code' sai kieu du lieu (number thay vi string)", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(R"({"code": 12345})");
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("kieu string") != std::string::npos ||
                     r->find("Loi") != std::string::npos,
                     "phai bao loi kieu du lieu ro rang: " + *r);
    });

    addTest("Args: 'code' la chuoi rong", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        auto r = tool.execute(args(""));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("rong") != std::string::npos, "phai bao 'code' rong: " + *r);
    });

    // ---- 6. Stateless ----------------------------------------------------
    addTest("Stateless: bien tu lan goi truoc KHONG duoc giu lai", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        tool.execute(args("x = 42"));
        auto r = tool.execute(args("print(x)"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("NameError") != std::string::npos, "bien 'x' bi giu lai qua lan goi khac: " + *r);
    });

    addTest("Stateless: import tu lan goi truoc KHONG duoc giu lai", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        tool.execute(args("import math"));
        auto r = tool.execute(args("print(math.pi)"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("NameError") != std::string::npos, "import 'math' bi giu lai qua lan goi khac: " + *r);
    });

    // ---- 7. Timeout ---------------------------------------------------
    addTest("Timeout: vong lap vo han bi huy dung han, khong treo AgentLoop", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, /*timeout*/2, 262144);
        auto start = std::chrono::steady_clock::now();
        auto r = tool.execute(args("while True:\n    pass"));
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(elapsed < 5, "cho qua lau (" + std::to_string(elapsed) + "s), timeout khong hoat dong")
            && check(r->find("timeout") != std::string::npos, "thieu canh bao timeout trong output: " + *r);
    });

    addTest("Timeout: gia tri 0 tu dong duoc ep ve 1 giay (khong tat han timeout)", [workspace] {
        // clampTimeoutSeconds(0) phai tra ve 1, KHONG duoc coi la "tat timeout"
        // (khac hanh vi cua GNU `timeout 0s` la chay vo han).
        CodeInterpreterTool tool([workspace] { return workspace; }, /*timeout*/0, 262144);
        auto start = std::chrono::steady_clock::now();
        auto r = tool.execute(args("while True:\n    pass"));
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(elapsed < 5, "timeoutSeconds=0 khong duoc ep ve 1s, cho qua lau: " + std::to_string(elapsed) + "s");
    });

    // ---- 8. Gioi han bo nho -------------------------------------------
    addTest("Memory: cap phat vuot gioi han bi chan (MemoryError/bi kill)", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, 3, /*memKb*/262144 /*256MB*/);
        auto r = tool.execute(args("a = bytearray(2 * 1024 * 1024 * 1024)\nprint('unreachable')"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("unreachable") == std::string::npos,
                     "cap phat 2GB LE RA phai bi chan boi gioi han 256MB nhung code van chay tiep: " + *r);
    });

    addTest("Memory: memory_limit_kb <= 0 nghia la KHONG gioi han", [workspace] {
        // Cap phat 100MB - se PASS khi khong co gioi han (memKb=0), trong khi
        // test truoc voi 256MB gioi han se chan cap phat 2GB.
        CodeInterpreterTool tool([workspace] { return workspace; }, 5, /*memKb*/0);
        auto r = tool.execute(args(
            "a = bytearray(100 * 1024 * 1024)\n"
            "print('allocated:', len(a))\n"));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("allocated: 104857600") != std::string::npos,
                     "cap phat 100MB phai THANH CONG khi memory_limit_kb=0 (khong gioi han): " + *r);
    });

    // ---- 9. Tich hop voi workspace / file that -------------------------
    addTest("Workspace: code doc/ghi file that trong thu muc workspace", [workspace] {
        fs::create_directories(workspace);
        CodeInterpreterTool tool([workspace] { return workspace; }, 3, 262144);
        auto r = tool.execute(args(
            "with open('note.txt', 'w') as f:\n"
            "    f.write('hello-from-code-interpreter')\n"
            "with open('note.txt') as f:\n"
            "    print(f.read())\n"));
        bool ok = check(r.has_value(), "khong co gia tri tra ve");
        ok &= check(r && r->find("hello-from-code-interpreter") != std::string::npos,
                     "khong doc lai duoc noi dung vua ghi: " + (r ? *r : "<null>"));
        fs::path note = fs::path(workspace) / "note.txt";
        ok &= check(fs::exists(note), "file note.txt phai ton tai that trong workspace sau khi chay");
        std::error_code ec;
        fs::remove(note, ec);
        return ok;
    });

    addTest("Workspace: don dep file tam (khong con snippet_*.py sot lai)", [workspace] {
        fs::create_directories(workspace);
        CodeInterpreterTool tool([workspace] { return workspace; }, 2, 262144);
        int before = countSnippetFiles(workspace);
        tool.execute(args("print('cleanup check')"));
        int after = countSnippetFiles(workspace);
        return check(after == before, "con sot file snippet_*.py trong workspace sau khi chay xong "
                                       "(truoc=" + std::to_string(before) + ", sau=" + std::to_string(after) + ")");
    });

    addTest("Workspace: duong dan chua dau nhay don (') khong pha vo lenh shell", [] {
        // Mo phong bug da fix: workspace/filePath co dau " truoc day co the
        // pha vo quote cu -> command injection. Gio dung shellQuote() an
        // toan cho MOI ky tu, kiem tra bang duong dan chua dau nhay don.
        std::string weird_ws = "/tmp/ci_test_it's_a_trap";
        std::error_code ec;
        fs::create_directories(weird_ws, ec);
        CodeInterpreterTool tool([weird_ws] { return weird_ws; }, 3, 262144);
        auto r = tool.execute(args("print('safe-even-with-quote-in-path')"));
        bool ok = check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("safe-even-with-quote-in-path") != std::string::npos,
                     "lenh bi vo vi duong dan chua dau nhay don: " + (r ? *r : "<null>"));
        fs::remove_all(weird_ws, ec);
        return ok;
    });

    addTest("Code content: ky tu dac biet shell ($ ` \" ; |) trong CODE van chay dung", [workspace] {
        // Code duoc ghi ra FILE that (data), khong noi vao chuoi lenh shell,
        // nen cac ky tu nay chi la text Python binh thuong, khong duoc shell
        // dien giai.
        CodeInterpreterTool tool([workspace] { return workspace; }, 3, 262144);
        std::string code =
            "s = 'contains $HOME `backtick` \"quote\" ; pipe | end'\n"
            "print(s)\n";
        auto r = tool.execute(args(code));
        return check(r.has_value(), "khong co gia tri tra ve")
            && check(r->find("contains $HOME `backtick` \"quote\" ; pipe | end") != std::string::npos,
                     "ky tu dac biet trong code bi shell dien giai sai: " + *r);
    });

    // ---- 10. Output rat lon bi cat bot -----------------------------------
    addTest("Output lon: bi cat bot khi vuot ~1MB, khong deadlock/treo", [workspace] {
        CodeInterpreterTool tool([workspace] { return workspace; }, /*timeout*/5, 262144);
        auto start = std::chrono::steady_clock::now();
        // In lien tuc de vuot 1MB (kMaxOutputBytes) truoc khi tu ket thuc.
        auto r = tool.execute(args(
            "for i in range(2_000_000):\n"
            "    print('x' * 50)\n"));
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        bool ok = check(r.has_value(), "khong co gia tri tra ve");
        ok &= check(elapsed < 10, "bi treo/deadlock khi output qua lon: " + std::to_string(elapsed) + "s");
        ok &= check(r && r->find("da bi cat bot") != std::string::npos,
                     "phai co canh bao output bi cat bot: " + std::string(r ? r->substr(0, 200) : "<null>"));
        return ok;
    });
}

// ════════════════════════════════════════════════════════════════
//  MAIN: chay tat ca test da dang ky, in bang tong ket
// ════════════════════════════════════════════════════════════════
int main() {
    registerAllTests();

    int passed = 0;
    int failed = 0;
    std::vector<std::string> failedNames;

    std::cout << "===== CodeInterpreterTool - Bo test toan dien =====\n";
    std::cout << "Tong so test: " << registry().size() << "\n\n";

    for (const auto& tc : registry()) {
        std::cout << "-- " << tc.name << "\n";
        bool ok = false;
        try {
            ok = tc.run();
        } catch (const std::exception& e) {
            std::cout << "    [EXCEPTION] " << e.what() << "\n";
            ok = false;
        } catch (...) {
            std::cout << "    [EXCEPTION] khong xac dinh\n";
            ok = false;
        }
        if (ok) {
            std::cout << "   [PASS]\n\n";
            ++passed;
        } else {
            std::cout << "   [FAIL]\n\n";
            ++failed;
            failedNames.push_back(tc.name);
        }
    }

    std::cout << "=====================================================\n";
    std::cout << "KET QUA: " << passed << "/" << registry().size() << " PASS";
    if (failed > 0) {
        std::cout << ", " << failed << " FAIL:\n";
        for (const auto& n : failedNames) std::cout << "   - " << n << "\n";
    } else {
        std::cout << " - TAT CA TEST DEU PASS\n";
    }
    std::cout << "=====================================================\n";

    return failed == 0 ? 0 : 1;
}