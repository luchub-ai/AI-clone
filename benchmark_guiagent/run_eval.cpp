// ════════════════════════════════════════════════════════════════
//  benchmark/run_eval_gui.cpp
//
//  Entry point CHO BENCHMARK GUI AGENT (bonus 10.1): doc
//  benchmark/gui_tasks.json -> dung ToolRegistry + LLMClient (vision) +
//  GUIAgentLoop + HarnessRunner (DA CO SAN trong src/, khong sua gi ca -
//  dung dung tinh than run_eval.cpp goc) de chay tung task -> cham diem
//  bang KeywordEvaluator/FunctionalEvaluator/VLMEvaluator -> xuat
//  trajectory + summary ra file JSON trong benchmark/gui_results/.
//
//  KHAC BIET SO VOI benchmark/run_eval.cpp (ban text-only):
//    1. AgentLoop dung la GUIAgentLoop thay vi ReActAgentLoop - nhung
//       goi harness.runBatch(tasks, agent) VAN GIONG HET, vi GUIAgentLoop
//       ke thua thang AgentLoop va KHONG them method public nao ngoai
//       nhung gi AgentLoop da khai bao (xem comment trong gui_agent_loop.h)
//       - HarnessRunner khong can biet/sua gi de chay duoc GUIAgentLoop.
//    2. Environment BAT BUOC la NativeEnvironment (supportsGui()==true) -
//       SandboxEnvironment (docker) khong co display/D-Bus session that
//       cua host nen ScreenshotTool/InputTool se luon fail. --env=sandbox
//       bi tu choi ngay tu dau thay vi de chay roi fail tung task.
//    3. Dang ky them InputTool ("gui_input") va ScreenshotTool duoc cau
//       hinh resize (--max-width) + scale toa do (--scale-x/--scale-y) -
//       xem GUI_AGENT_SETUP.md de biet cach xac dinh 2 gia tri scale nay
//       cho dung may dang chay benchmark.
//    4. Model mac dinh la model CO VISION (vd qwen3-vl:8b) thay vi model
//       text-only - model khong ho tro vision van chay duoc (khong crash)
//       nhung se "mu" truoc moi anh chup man hinh, GUIAgentLoop khong tu
//       phat hien duoc dieu nay.
//    5. --client=ollama|colab: chon LLMClient cu the (ColabClient dung
//       cho model vision chay tren Colab qua tunnel ngrok/cloudflare,
//       xem client/colab_client.h) - --base-url doi y nghia tuong ung
//       (localhost:11434 cho Ollama, URL tunnel cho Colab).
//
//  Tasks trong gui_tasks.json dung CHUNG dinh dang voi tasks.json (field
//  "image_base64" optional van duoc doc san trong loadTasks() ben duoi -
//  neu task tu cung cap anh, GUIAgentLoop van hoat dong binh thuong vi no
//  chi tu chup anh o turn 1 KHI conversation_history dung 2 message, tuc
//  la KHI task.images_base64 dang rong; task da co san anh thi bo qua
//  buoc tu chup, dung luon anh nguoi ra de cung cap).
//
//  ------------------------------------------------------------------
//  CACH CHAY (sau khi build bang CMake, xem README):
//    ./build/run_eval_gui
//    ./build/run_eval_gui --tasks=benchmark/gui_tasks.json --out=benchmark/gui_results
//    ./build/run_eval_gui --model=qwen3-vl:8b --base-url=http://localhost:11434
//    ./build/run_eval_gui --client=colab --base-url=https://xxxx.trycloudflare.com --model=qwen3-vl:8b
//    ./build/run_eval_gui --max-width=1280 --scale-x=1.0 --scale-y=1.0
//    ./build/run_eval_gui --tools=exec,file,gui_input,capture_screenshot
//
//  YEU CAU BO SUNG SO VOI run_eval.cpp (vi day la benchmark GUI THAT tren
//  desktop that):
//    - ydotoold dang chay (systemctl --user status ydotoold) va
//      $YDOTOOL_SOCKET da duoc set trong shell chay binary nay.
//    - Da cap quyen xdg-desktop-portal Screenshot 1 lan (chay thu
//      capture_screenshot don le neu chua chac chan, xem
//      GUI_AGENT_SETUP.md muc 2) - neu chua, TASK DAU TIEN cua batch se
//      dung lai cho toi 60s de nguoi dung bam Allow tren man hinh that,
//      cac task sau do se khong bi nua.
//    - Khong dung chuot/ban phim thu cong trong luc benchmark dang chay -
//      InputTool dieu khien input THAT, se xung dot voi thao tac tay.
// ════════════════════════════════════════════════════════════════

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "src/tools/tool_registry.h"
#include "src/tools/calculator_tool.h"
#include "src/tools/exec_tool.h"
#include "src/tools/file_tool.h"
#include "src/tools/memory_tool.h"
#include "src/tools/input_tool.h"
#include "src/tools/screenshot_tool.h"

#include "src/client/ollama_client.h"
#include "src/client/colab_client.h"
#include "src/agent/gui_agent_loop.h"
#include "src/agent/skill_loader.h"
#include "src/agent/loop_detector.h"
#include "src/harness/harness_runner.h"
#include "src/harness/environment/environment.h"
#include "src/harness/evaluator/evaluator.h"
#include "src/common/task.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// ── Cau hinh dong lenh / bien moi truong ──────────────────────────
struct CliOptions {
    std::string tasks_path = "benchmark_guiagent/gui_tasks.json";
    std::string out_dir    = "benchmark_guiagent/gui_results";
    std::string base_url   = "http://localhost:11434";
    std::string model      = "gemma4:e4b"; // model CO VISION - doi neu ban dung model khac
    std::string client     = "ollama";      // "ollama" | "colab"
    float       temperature = 0.2f;         // huong dan visual can it "sang tao" hon text task thuan
    int         max_tokens  = 2048;
    std::string env_mode    = "native";     // BAT BUOC native cho GUI - xem check ben duoi main()
    std::vector<std::string> allowed_tools; // rong = cho phep tat ca tool da dang ky

    // Rieng cho GUI:
    std::optional<int> screenshot_max_width; // rong = KHONG resize anh chup man hinh
    double scale_x = 2.5; // he so bu tru sai lech ydotool --absolute - xem GUI_AGENT_SETUP.md
    double scale_y = 2.5;
};

std::optional<std::string> eatFlagValue(const std::string& arg, const std::string& prefix) {
    if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
    return std::nullopt;
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions opt;

    if (const char* v = std::getenv("OLLAMA_BASE_URL")) opt.base_url = v;
    if (const char* v = std::getenv("OLLAMA_MODEL"))    opt.model    = v;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (auto v = eatFlagValue(arg, "--tasks="))         { opt.tasks_path  = *v; continue; }
        if (auto v = eatFlagValue(arg, "--out="))           { opt.out_dir     = *v; continue; }
        if (auto v = eatFlagValue(arg, "--base-url="))      { opt.base_url    = *v; continue; }
        if (auto v = eatFlagValue(arg, "--model="))         { opt.model       = *v; continue; }
        if (auto v = eatFlagValue(arg, "--client="))        { opt.client      = *v; continue; }
        if (auto v = eatFlagValue(arg, "--env="))           { opt.env_mode    = *v; continue; }
        if (auto v = eatFlagValue(arg, "--temperature="))   { opt.temperature = std::stof(*v); continue; }
        if (auto v = eatFlagValue(arg, "--max-tokens="))    { opt.max_tokens  = std::stoi(*v); continue; }
        if (auto v = eatFlagValue(arg, "--max-width="))     { opt.screenshot_max_width = std::stoi(*v); continue; }
        if (auto v = eatFlagValue(arg, "--scale-x="))       { opt.scale_x     = std::stod(*v); continue; }
        if (auto v = eatFlagValue(arg, "--scale-y="))       { opt.scale_y     = std::stod(*v); continue; }
        if (auto v = eatFlagValue(arg, "--tools=")) {
            std::stringstream ss(*v);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (!tok.empty()) opt.allowed_tools.push_back(tok);
            }
            continue;
        }
        std::cerr << "[run_eval_gui] Bo qua tham so khong nhan dien: " << arg << "\n";
    }
    return opt;
}

// ── Doc benchmark/gui_tasks.json -> vector<Task> ──────────────────
// Y HET loadTasks() trong run_eval.cpp - tasks.json dung chung dinh dang,
// field "image_base64" da duoc doc san tu truoc (khong can them gi).
std::vector<Task> loadTasks(const std::string& path,
                             std::map<std::string, std::string>& descriptions_out) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("Khong mo duoc file tasks: " + path);
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("tasks.json khong phai JSON hop le: ") + e.what());
    }
    if (!j.is_array()) {
        throw std::runtime_error("tasks.json phai la 1 JSON array o cap cao nhat.");
    }

    std::vector<Task> tasks;
    tasks.reserve(j.size());
    for (const auto& item : j) {
        Task t;
        t.id          = item.value("id", "");
        t.instruction = item.value("instruction", "");
        t.eval_type   = item.value("eval_type", "keyword");
        t.eval_script = item.value("eval_script", "");
        t.max_steps   = item.value("max_steps", 15); // GUI thuong can nhieu turn hon text task

        std::string temp = item.value("image_base64", "");
        if (!t.id.empty() && !temp.empty()) {
            t.images_base64 = temp;
        }

        if (t.id.empty() || t.instruction.empty()) {
            std::cerr << "[run_eval_gui] Bo qua task thieu id/instruction: " << item.dump() << "\n";
            continue;
        }
        descriptions_out[t.id] = item.value("description", "");
        tasks.push_back(std::move(t));
    }
    return tasks;
}

} // namespace

int main(int argc, char** argv) {
    CliOptions opt = parseArgs(argc, argv);

    // === 0. GUI benchmark BAT BUOC NativeEnvironment ===
    // SandboxEnvironment (docker) khong co display/D-Bus session/uinput
    // that cua host - ScreenshotTool/InputTool se fail 100% task neu chay
    // trong do. Chan tu day thay vi de HarnessRunner chay roi moi fail
    // tung task mot cach kho hieu.
    if (opt.env_mode == "sandbox") {
        std::cerr << "[run_eval_gui] LOI: --env=sandbox khong duoc ho tro cho benchmark GUI.\n"
                     "[run_eval_gui] ScreenshotTool/InputTool can display/D-Bus/uinput THAT cua "
                     "host, khong co trong container docker.\n"
                     "[run_eval_gui] Bo han --env (mac dinh da la native) hoac dung ro "
                     "--env=native.\n";
        return 1;
    }

    // === 1. Doc tap task ===
    std::map<std::string, std::string> descriptions;
    std::vector<Task> tasks;
    try {
        tasks = loadTasks(opt.tasks_path, descriptions);
    } catch (const std::exception& e) {
        std::cerr << "[run_eval_gui] Loi doc tasks.json: " << e.what() << "\n";
        return 1;
    }
    if (tasks.empty()) {
        std::cerr << "[run_eval_gui] Khong co task hop le nao trong " << opt.tasks_path << "\n";
        return 1;
    }
    std::cout << "[run_eval_gui] Da nap " << tasks.size() << " task tu " << opt.tasks_path << "\n";

    // === 2. Environment (luon NativeEnvironment) ===
    auto env = std::make_unique<NativeEnvironment>("benchmark_guiagent/workspace/gui_native_workspace");
    if (!env->supportsGui()) {
        // Phong ho: neu sau nay co ai doi supportsGui() cua NativeEnvironment
        // thanh false vi ly do gi do, benchmark GUI phai bao loi ro rang
        // ngay tu dau, khong duoc chay ngam roi fail moi task.
        std::cerr << "[run_eval_gui] LOI: NativeEnvironment::supportsGui() dang tra ve false "
                     "tren may nay - khong the chay benchmark GUI.\n";
        return 1;
    }

    // Lay raw pointer TRUOC KHI move vao HarnessRunner - dung nguyen pattern
    // da duoc fix trong run_eval.cpp (xem comment [SUA - BUG #2] o do): cac
    // lambda cua tool ben duoi CHi giu con tro tho, khong giu unique_ptr, nen
    // van hop le sau khi std::move(env) vao HarnessRunner.
    Environment* env_ptr = env.get();

    // === 3. Tool registry ===
    auto tools = std::make_shared<ToolRegistry>();
    tools->registerTool(std::make_unique<CalculatorTool>());
    tools->registerTool(std::make_unique<MemoryTool>());
    tools->registerTool(std::make_unique<ExecTool>(env_ptr, 5, false)); // timeout 5s - can de mo app (vd xdg-open)
    tools->registerTool(std::make_unique<FileTool>(env_ptr));

    if (env_ptr->supportsGui()) {
        CommandRunner input_runner = [env_ptr](const std::string& cmd) {
            return env_ptr->execute(cmd, /*timeoutSeconds=*/5);
        };
        tools->registerTool(std::make_unique<InputTool>(input_runner, opt.scale_x, opt.scale_y));
        tools->registerTool(std::make_unique<ScreenshotTool>(
            [env_ptr]() { return env_ptr->getWorkspace();}
        ));
    } else {
        std::cerr << "[run_eval_gui] CANH BAO: supportsGui() == false - InputTool/"
                     "ScreenshotTool KHONG duoc dang ky (khong nen xay ra vi da check o tren).\n";
    }

    if (!opt.allowed_tools.empty()) {
        tools->setAllowedTools(opt.allowed_tools);
    }

    // === 4. LLM client cho agent (can model CO VISION) ===
    std::unique_ptr<LLMClient> llm;
    if (opt.client == "colab") {
        llm = std::make_unique<ColabClient>(opt.base_url, opt.model, opt.temperature, opt.max_tokens);
    } else {
        llm = std::make_unique<OllamaClient>(opt.base_url, opt.model, opt.temperature, opt.max_tokens);
    }

    // === 5. Skill loader ===
    auto skills = std::make_unique<SkillLoader>("skills");
    skills->loadSkillsFromDisk();

    // === 6. Loop detector: canh bao sau 2 lan lap, dung han sau 3 lan ===
    auto loop_detector = std::make_unique<LoopDetector>(2, 3);

    // === 7. Agent loop (GUI) ===
    // GUIAgentLoop KHONG them method public nao ngoai AgentLoop (xem
    // gui_agent_loop.h) - HarnessRunner ben duoi goi runBatch(tasks, agent)
    // Y HET nhu voi ReActAgentLoop trong run_eval.cpp, khong sua gi ca.
    GUIAgentLoop agent(std::move(llm), tools, std::move(skills), std::move(loop_detector));

    // === 8. HarnessRunner + dang ky evaluator theo eval_type ===
    HarnessRunner harness(std::move(env));
    harness.registerEvaluator("keyword", std::make_unique<KeywordEvaluator>());
    harness.registerEvaluator("functional", std::make_unique<FunctionalEvaluator>());
    // VLMEvaluator lam giam khao rieng cho GUI benchmark hop ly hon ca -
    // vd cham "man hinh cuoi cung co dung hien ket qua can tim khong" -
    // thu tot hon KeywordEvaluator (chi tim tu trong text output).
    harness.registerEvaluator("vlm", std::make_unique<VLMEvaluator>(
        std::make_unique<OllamaClient>(opt.base_url, opt.model, 0.0f, 256)));

    // === 9. Chay batch ===
    try {
        harness.runBatch(tasks, agent);
    } catch (const std::exception& e) {
        std::cerr << "\n[run_eval_gui] LOI: khong the chay batch - " << e.what() << "\n";
        std::cerr << "[run_eval_gui] Kiem tra: ydotoold dang chay chua "
                     "(systemctl --user status ydotoold), $YDOTOOL_SOCKET da set chua, "
                     "va da cap quyen xdg-desktop-portal Screenshot chua.\n";
        return 1;
    }

    // === 10. Xuat ket qua ra JSON ===
    fs::create_directories(opt.out_dir);

    int   total = 0, succeeded = 0, scored = 0, crashed = 0;
    float score_sum = 0.0f;
    json  summary_tasks = json::array();

    for (const auto& r : harness.getResults()) {
        ++total;
        if (r.agent_crashed) ++crashed;
        if (r.task_success)  ++succeeded;
        if (r.score) { ++scored; score_sum += *r.score; }

        std::string traj_filename = "trajectory_" + r.task_id + ".json";
        std::string traj_path     = opt.out_dir + "/" + traj_filename;
        std::ofstream ofs(traj_path);
        if (ofs) {
            ofs << r.trajectory.exportToJson();
        } else {
            std::cerr << "[run_eval_gui] Khong ghi duoc " << traj_path << "\n";
        }

        summary_tasks.push_back({
            {"task_id",            r.task_id},
            {"description",        descriptions.count(r.task_id) ? descriptions.at(r.task_id) : ""},
            {"crashed",            r.agent_crashed},
            {"trajectory_success", r.trajectory.success},
            {"score",              r.score ? json(*r.score) : json(nullptr)},
            {"score_error",        r.score ? json(nullptr) : json(std::string(toString(r.score.error())))},
            {"task_success",       r.task_success},
            {"trajectory_file",    traj_filename}
        });
    }

    float success_rate = (total > 0)
        ? static_cast<float>(succeeded) / static_cast<float>(total)
        : 0.0f;

    json summary = {
        {"agent_type",      "GUIAgentLoop"},
        {"model",           opt.model},
        {"client",          opt.client},
        {"base_url",        opt.base_url},
        {"screenshot_max_width", opt.screenshot_max_width ? json(*opt.screenshot_max_width) : json(nullptr)},
        {"scale_x",         opt.scale_x},
        {"scale_y",         opt.scale_y},
        {"total_tasks",     total},
        {"crashed_tasks",   crashed},
        {"scored_tasks",    scored},
        {"succeeded_tasks", succeeded},
        {"success_rate",    success_rate},
        {"average_score",   scored > 0 ? json(score_sum / static_cast<float>(scored)) : json(nullptr)},
        {"tasks",           summary_tasks}
    };

    std::string summary_path = opt.out_dir + "/summary.json";
    std::ofstream ofs(summary_path);
    if (ofs) {
        ofs << summary.dump(4);
    } else {
        std::cerr << "[run_eval_gui] Khong ghi duoc " << summary_path << "\n";
    }

    std::cout << "\n========================================\n"
              << "[run_eval_gui] Success rate: " << (success_rate * 100.0f) << "% ("
              << succeeded << "/" << total << " task)\n"
              << "[run_eval_gui] Ket qua tong hop: " << summary_path << "\n"
              << "[run_eval_gui] Trajectory tung task: " << opt.out_dir << "/trajectory_<task_id>.json\n"
              << "========================================\n";

    return 0;
}