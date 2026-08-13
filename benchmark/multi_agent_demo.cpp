// ════════════════════════════════════════════════════════════════
//  benchmark/multi_agent_demo.cpp
//
//  Demo cho de bai 10.3 "Multi-agent Coordination" (+3d).
//
//  KHONG dung chung file/executable voi benchmark/run_eval.cpp (khong
//  sua run_eval.cpp mot dong nao) - day la 1 executable RIENG, dang ky
//  them trong CMakeLists.txt, dung LAI 100% code da co san trong src/
//  (ToolRegistry, moi Tool, OllamaClient, ReActAgentLoop, HarnessRunner,
//  Environment...) giong dung tinh than "wiring" cua run_eval.cpp - chi
//  THEM 2 thu moi: MessageQueue/MessageQueueTool (src/common,
//  src/tools) va HarnessRunner::runParallel() (them vao
//  harness_runner.h/.cpp, khong dong nao cu bi sua).
//
//  ------------------------------------------------------------------
//  QUYET DINH THIET KE QUAN TRONG NHAT (quyet truoc khi viet dong code
//  nao, dung nguyen van tinh than de bai 10.3):
//
//    Moi sub-agent PHAI co ToolRegistry + Environment RIENG cua no,
//    TUYET DOI khong share 1 bo tool giua 2 thread. Ly do cu the nhin
//    tu CHINH code hien co trong repo nay, khong phai ly thuyet suong:
//
//      - MemoryTool (src/tools/memory_tool.h) cam 1 sqlite3* mo 1 lan
//        trong constructor, khong co mutex nao bao quanh
//        sqlite3_prepare_v2/sqlite3_step - 2 thread goi CHUNG 1
//        connection nay khong dong bo la undefined behavior.
//      - WebBrowserTool (src/tools/web_browser_tool.h) tu nhan la
//        stateful (1 session_id_ WebDriver song xuyen nhieu execute())
//        va CO CHU Y xoa copy constructor voi ly do "1 session chi nen
//        do 1 instance quan ly" - 2 agent lai chung 1 browser cung luc
//        la chac chan hong.
//      - Neu share Environment (NativeEnvironment/SandboxEnvironment),
//        ca 2 agent ghi de len cung 1 workspace, de da nhau.
//
//    Share het moi thu chi to phai va mutex vao 5-6 class Tool san co
//    (rui ro cao, khong dang +3d). Chi co DUNG 1 thu that su can chia
//    se va thread-safe theo dung nghia: MessageQueue
//    (src/common/message_queue.h) - xem class do de biet vi sao chinh
//    no lai an toan de share con nhung cai kia thi khong.
//
//    2 diem THEM (ngoai 3 gach dau dong tren, phat hien khi doi chieu
//    voi CHINH code hien co, anh huong truc tiep cach wiring ben duoi):
//
//      1. NativeEnvironment KHONG tu tranh trung workspace: constructor
//         co gia tri mac dinh dir="workspace/native_workspace" CO DINH.
//         Neu 2 sub-agent deu make_unique<NativeEnvironment>() KHONG
//         truyen path rieng, 2 OBJECT C++ tach biet van tro chung 1 thu
//         muc tren dia - van da nhau y het truong hop share 1
//         Environment (khac SandboxEnvironment - cai do dung mkdtemp()
//         nen tu dong unique du cung prefix). -> Ben duoi TRUYEN
//         DUONG DAN RIENG cho tung sub-agent.
//      2. MemoryTool cung y het van de do: constructor co
//         db_path="agent_memory.db" CO DINH lam mac dinh. 2
//         MemoryTool() khong tham so se cung mo 1 file .db, sinh loi
//         SQLITE_BUSY khac thuong khi 2 connection ghi trung luc (khong
//         phai UB o muc C API vi SQLite tu ho tro nhieu connection/1
//         file, nhung la 1 loi logic ro rang pha vo dung y "moi
//         sub-agent 1 the gioi rieng"). -> Ben duoi TRUYEN db_path
//         RIENG cho tung sub-agent.
//
//  ------------------------------------------------------------------
//  KICH BAN DEMO: 1 task duoc chia lam 2 nua, giao cho 2 sub-agent chay
//  DONG THOI qua HarnessRunner::runParallel():
//
//    agent_a ("nguoi tinh toan"): dung calculator_tool tinh tong binh
//      phuong 1..10 (= 385), roi dung message_queue (action="send")
//      gui ket qua cho agent_b.
//    agent_b ("nguoi luu tru"): dung message_queue (action="receive")
//      cho tin tu agent_a, luu lai bang memory_tool (action="save"),
//      roi tra loi Final Answer dung con so vua nhan.
//
//  agent_b_task duoc dang ky KeywordEvaluator voi eval_script="385" -
//  CHI khop neu "385" thuc su xuat hien trong trajectory cua agent_b
//  (thought hoac tool_result), tuc CHI pass neu message tu agent_a
//  THUC SU den duoc agent_b qua MessageQueue that (khong phai agent_b
//  "doan mo" ra 385 - de bai khong nhac so nay o dau trong instruction
//  cua agent_b).
//
//  Ca 2 sub-agent van duoc dang ky DAY DU bo tool nhu run_eval.cpp
//  (calculator/exec/file/memory/web_search/screenshot/code_interpreter/
//  web_browser + message_queue moi) de chung minh THAT SU moi agent co
//  1 ban rieng cho TAT CA tool, khong chi rieng 2 tool duoc nhac toi o
//  tren - nhung kich ban demo CHI bat buoc calculator/memory/
//  message_queue thanh cong (web_search/web_browser dang ky binh
//  thuong nhung khong bat buoc phai dung duoc, giong tinh than
//  run_eval.cpp, de demo khong bi phu thuoc chromedriver/Tavily API key
//  luc bao ve truoc giao vien).
//
//  ------------------------------------------------------------------
//  CACH CHAY (sau khi build bang CMake, xem README + phan CMakeLists
//  cua target nay):
//    ./build/multi_agent_demo
//    ./build/multi_agent_demo --model=qwen3:8b --base-url=http://localhost:11434
//    ./build/multi_agent_demo --max-steps=15 --receive-timeout-ms=60000
//
//  YEU CAU: Ollama phai dang chay that (`ollama serve` + model da
//  pull), giong het yeu cau cua run_eval.cpp - day la demo CHAY THAT,
//  khong phai mock.
// ════════════════════════════════════════════════════════════════

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "src/tools/tool_registry.h"
#include "src/tools/calculator_tool.h"
#include "src/tools/exec_tool.h"
#include "src/tools/file_tool.h"
#include "src/tools/web_search_tool.h"
#include "src/tools/memory_tool.h"
#include "src/tools/screenshot_tool.h"
#include "src/tools/code_interpreter_tool.h"
#include "src/tools/web_browser_tool.h"
#include "src/tools/message_queue_tool.h"

#include "src/client/ollama_client.h"
#include "src/agent/react_agent_loop.h"
#include "src/agent/skill_loader.h"
#include "src/agent/loop_detector.h"
#include "src/harness/harness_runner.h"
#include "src/harness/environment/environment.h"
#include "src/harness/evaluator/evaluator.h"
#include "src/common/task.h"
#include "src/common/message_queue.h"

namespace fs = std::filesystem;

namespace {

// ── Cau hinh dong lenh / bien moi truong - rut gon tu CliOptions cua
//    run_eval.cpp (day khong phai benchmark day du, khong can het cac
//    co --tools=/--out= cua file do) ────────────────────────────────
struct DemoOptions {
    std::string base_url = "http://localhost:11434";
    std::string model    = "gemma4:e4b";
    float       temperature = 0.0f;
    int         max_tokens  = 2048;
    int         max_steps   = 10;
    int         receive_timeout_ms = 45000; // agent_b cho toi da 45s moi lan goi receive
    std::string tavily_api_key;             // chi doc tu env, giong run_eval.cpp
    std::string chromedriver_url = "http://127.0.0.1:9515";
    std::string browser_binary_path;
};

std::optional<std::string> eatFlagValue(const std::string& arg, const std::string& prefix) {
    if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
    return std::nullopt;
}

DemoOptions parseArgs(int argc, char** argv) {
    DemoOptions opt;
    if (const char* v = std::getenv("OLLAMA_BASE_URL"))     opt.base_url = v;
    if (const char* v = std::getenv("OLLAMA_MODEL"))        opt.model    = v;
    if (const char* v = std::getenv("TAVILY_API_KEY"))      opt.tavily_api_key = v;
    if (const char* v = std::getenv("CHROMEDRIVER_URL"))    opt.chromedriver_url = v;
    if (const char* v = std::getenv("BROWSER_BINARY_PATH")) opt.browser_binary_path = v;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (auto v = eatFlagValue(arg, "--base-url="))          { opt.base_url = *v; continue; }
        if (auto v = eatFlagValue(arg, "--model="))             { opt.model    = *v; continue; }
        if (auto v = eatFlagValue(arg, "--max-steps="))         { opt.max_steps = std::stoi(*v); continue; }
        if (auto v = eatFlagValue(arg, "--receive-timeout-ms=")){ opt.receive_timeout_ms = std::stoi(*v); continue; }
        std::cerr << "[multi_agent_demo] Bo qua tham so khong nhan dien: " << arg << "\n";
    }
    return opt;
}

// ── Dung 1 ban tool registry HOAN TOAN MOI cho 1 sub-agent ─────────
// Goi ham nay 2 LAN RIENG BIET (1 lan/sub-agent, xem main()) - moi lan
// goi tao ra NHUNG INSTANCE TOOL MOI HOAN TOAN (registerTool nhan
// unique_ptr, khong the vo tinh dung lai object cu du co goi ham nay
// nhieu lan voi cung tham so). Day chinh la co che dam bao "moi
// sub-agent 1 ToolRegistry rieng" o muc code, khong chi o muc y dinh.
//
// env: Environment CUA RIENG sub-agent nay (KHONG so huu - nguoi goi
//   (main()) giu unique_ptr, chi truyen raw pointer xuong day dung y
//   het cach run_eval.cpp lam voi env.get()).
// agent_id: dinh danh sub-agent nay - dung lam agent_id cho
//   MessageQueueTool cua NO (KHONG phai id cua ai khac).
// mq: shared_ptr<MessageQueue> DUY NHAT duoc truyen CHUNG cho ca 2 lan
//   goi ham nay (xem main()) - day la DIEM CHIA SE DUY NHAT trong toan
//   bo wiring nay.
std::shared_ptr<ToolRegistry> buildSubAgentTools(Environment* env,
                                                   const std::string& agent_id,
                                                   std::shared_ptr<MessageQueue> mq,
                                                   const DemoOptions& opt) {
    auto tools = std::make_shared<ToolRegistry>();

    tools->registerTool(std::make_unique<CalculatorTool>());
    // MemoryTool: db_path RIENG theo agent_id - xem giai thich diem (2)
    // o dau file, KHONG duoc de mac dinh "agent_memory.db" chung.
    tools->registerTool(std::make_unique<MemoryTool>("benchmark/workspace/multi_agent_" + agent_id + "_memory.db"));
    tools->registerTool(std::make_unique<ExecTool>(env));
    tools->registerTool(std::make_unique<FileTool>(env));
    // Dang ky binh thuong du tavily_api_key/chromedriver co the rong -
    // chi bao loi ro rang khi agent THUC SU goi toi, dung tinh than
    // run_eval.cpp (xem comment dau file ve tinh "khong bat buoc").
    tools->registerTool(std::make_unique<WebSearchTool>(opt.tavily_api_key));
    tools->registerTool(std::make_unique<ScreenshotTool>([env]() { return env->getWorkspace(); }));
    tools->registerTool(std::make_unique<CodeInterpreterTool>(
        [env]() { return env->getWorkspace(); }, 10, 512 * 1024, "python3"));
    tools->registerTool(std::make_unique<WebBrowserTool>(opt.chromedriver_url, opt.browser_binary_path));

    // Tool DUY NHAT dung chung 1 vat gi do (mq) giua 2 sub-agent - xem
    // message_queue_tool.h de biet vi sao dieu nay an toan con cac tool
    // tren thi khong.
    tools->registerTool(std::make_unique<MessageQueueTool>(std::move(mq), agent_id));

    return tools;
}

} // namespace

int main(int argc, char** argv) {
    DemoOptions opt = parseArgs(argc, argv);

    std::cout << "========================================\n"
                 "  Multi-agent Coordination Demo (10.3)\n"
                 "========================================\n"
                 "Model: " << opt.model << "  @ " << opt.base_url << "\n"
                 "2 sub-agent se chay SONG SONG tren 2 thread rieng,\n"
                 "chi chia se 1 MessageQueue chung.\n"
                 "========================================\n\n";

    // === Vat CHIA SE DUY NHAT giua 2 sub-agent ===
    auto mq = std::make_shared<MessageQueue>();

    // === Sub-agent "agent_a": tinh toan roi gui ket qua ===
    // Path RIENG - xem diem (1) o dau file (NativeEnvironment KHONG tu
    // tranh trung workspace nhu SandboxEnvironment).
    auto env_a = std::make_unique<NativeEnvironment>("benchmark/workspace/multi_agent_agent_a");
    Environment* env_a_ptr = env_a.get();
    auto tools_a = buildSubAgentTools(env_a_ptr, "agent_a", mq, opt);
    auto llm_a          = std::make_unique<OllamaClient>(opt.base_url, opt.model, opt.temperature, opt.max_tokens);
    auto skills_a       = std::make_unique<SkillLoader>("skills");
    skills_a->loadSkillsFromDisk();
    auto loop_detector_a = std::make_unique<LoopDetector>(2, 3);
    ReActAgentLoop agent_a(std::move(llm_a), tools_a, std::move(skills_a), std::move(loop_detector_a));

    Task task_a{
        .id = "multi_agent_a",
        .instruction =
            "Ban la agent_a trong 1 he thong 2 agent. Nhiem vu cua ban:\n"
            "1. Dung cong cu calculator de tinh tong binh phuong cac so nguyen tu 1 den 10 "
            "(tuc 1^2 + 2^2 + ... + 10^2).\n"
            "2. Sau khi co ket qua, dung cong cu message_queue voi "
            "{\"action\": \"send\", \"to\": \"agent_b\", \"content\": \"<ket qua dang so>\"} "
            "de gui ket qua do cho agent_b.\n"
            "3. Sau khi gui xong, tra loi Final Answer chinh la ket qua da tinh duoc.",
        .eval_type   = "keyword",
        .eval_script = "385", // 1^2+...+10^2 = 385 - xem cong thuc trong comment dau file
        .max_steps   = opt.max_steps,
    };

    // === Sub-agent "agent_b": cho nhan roi luu lai ===
    auto env_b = std::make_unique<NativeEnvironment>("benchmark/workspace/multi_agent_agent_b");
    Environment* env_b_ptr = env_b.get();
    auto tools_b = buildSubAgentTools(env_b_ptr, "agent_b", mq, opt);
    auto llm_b          = std::make_unique<OllamaClient>(opt.base_url, opt.model, opt.temperature, opt.max_tokens);
    auto skills_b       = std::make_unique<SkillLoader>("skills");
    skills_b->loadSkillsFromDisk();
    auto loop_detector_b = std::make_unique<LoopDetector>(2, 3);
    ReActAgentLoop agent_b(std::move(llm_b), tools_b, std::move(skills_b), std::move(loop_detector_b));

    Task task_b{
        .id = "multi_agent_b",
        .instruction =
            "Ban la agent_b trong 1 he thong 2 agent. Nhiem vu cua ban:\n"
            "1. Dung cong cu message_queue voi "
            "{\"action\": \"receive\", \"timeout_ms\": " + std::to_string(opt.receive_timeout_ms) + "} "
            "de cho nhan message tu agent_a. Neu ket qua bao 'Khong co message nao', "
            "hay goi lai action receive them 1-2 lan nua (agent_a co the dang tinh toan).\n"
            "2. Khi da nhan duoc noi dung (1 con so), dung cong cu memory_tool voi "
            "{\"action\": \"save\", \"data\": \"<noi dung vua nhan>\"} de luu lai.\n"
            "3. Tra loi Final Answer chinh la con so vua nhan duoc tu agent_a.",
        .eval_type   = "keyword",
        .eval_script = "385", // CHI khop neu message that su den tu agent_a - xem comment dau file
        .max_steps   = opt.max_steps,
    };

    // === HarnessRunner ===
    // env_ ben duoi CHI de thoa constructor (bat buoc 1 Environment hop
    // le - xem harness_runner.cpp) - runParallel() KHONG dung den env_
    // nay, moi sub-agent da tu mang Environment RIENG cua no trong
    // SubAgentJob roi (env_a/env_b o tren). Dat ten ro "unused" +
    // path rieng de khong bi nham hay vo tinh trung voi 2 cai tren.
    HarnessRunner harness(std::make_unique<NativeEnvironment>("benchmark/workspace/multi_agent_demo_unused"));
    harness.registerEvaluator("keyword", std::make_unique<KeywordEvaluator>());

    std::vector<HarnessRunner::SubAgentJob> jobs;
    jobs.push_back(HarnessRunner::SubAgentJob{
        .agent_id = "agent_a", .env = std::move(env_a), .loop = &agent_a, .task = task_a});
    jobs.push_back(HarnessRunner::SubAgentJob{
        .agent_id = "agent_b", .env = std::move(env_b), .loop = &agent_b, .task = task_b});

    std::cout << "[multi_agent_demo] Dang spawn " << jobs.size() << " sub-agent, chay song song...\n\n";

    auto started_at = std::chrono::steady_clock::now();
    std::vector<HarnessRunner::SubAgentResult> results;
    try {
        results = harness.runParallel(std::move(jobs));
    } catch (const std::exception& e) {
        std::cerr << "\n[multi_agent_demo] LOI: khong the chay runParallel - " << e.what() << "\n";
        return 1;
    }
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - started_at)
                          .count();

    // === In + xuat ket qua ===
    fs::create_directories("benchmark/results");

    std::cout << "\n========================================\n"
                 "KET QUA MULTI-AGENT (chay het " << elapsed_ms << "ms)\n"
                 "========================================\n";

    bool all_success = !results.empty();
    for (const auto& sub : results) {
        std::cout << "  " << sub.agent_id << " (task " << sub.result.task_id << "): ";
        if (sub.result.agent_crashed) {
            std::cout << "CRASHED\n";
            all_success = false;
        } else {
            std::cout << "score=" << (sub.result.score ? std::to_string(*sub.result.score) : "N/A")
                       << " task_success=" << (sub.result.task_success ? "true" : "false") << "\n";
            all_success = all_success && sub.result.task_success;
        }

        std::string traj_path = "benchmark/results/trajectory_multi_agent_" + sub.agent_id + ".json";
        std::ofstream ofs(traj_path);
        if (ofs) {
            ofs << sub.result.trajectory.exportToJson();
            std::cout << "    -> trajectory: " << traj_path << "\n";
        }
    }

    std::cout << "========================================\n";
    if (all_success) {
        std::cout << "[multi_agent_demo] THANH CONG: agent_b nhan dung ket qua (385) "
                     "tu agent_a qua MessageQueue - 2 agent chay tren 2 Environment/"
                     "ToolRegistry hoan toan doc lap.\n";
    } else {
        std::cout << "[multi_agent_demo] Chua thanh cong het - xem trajectory JSON o tren "
                     "de debug (co the do model yeu khong theo dung dinh dang ReAct, hoac "
                     "can tang --max-steps/--receive-timeout-ms).\n";
    }

    return all_success ? 0 : 1;
}
