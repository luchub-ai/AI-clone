#include "src/agent/gui_agent_loop.h"
#include "src/tools/input_tool.h"
#include "src/tools/screenshot_tool.h"
#include "src/tools/tool_registry.h"
#include "src/harness/environment/environment.h"
#include "src/tools/exec_tool.h"
#include "src/tools/file_tool.h"
#include "src/client/ollama_client.h"
#include "src/client/colab_client.h"
#include <iostream>


// TODO: doi ten class LLMClient/SkillLoader/LoopDetector cu the ban dang
// dung thay cho cac placeholder duoi day (ban da co san o main.cpp that,
// chi copy nguyen doan khoi tao do sang, KHONG can doi gi).

int main(int argc, char** argv) {
    auto environment = std::make_shared<NativeEnvironment>();
    environment->setup();

    auto tools = std::make_shared<ToolRegistry>();
    tools->registerTool(std::make_unique<ExecTool>(environment.get(),2,false));
    tools->registerTool(std::make_unique<FileTool>(environment.get()));

    if (environment->supportsGui()) {
        CommandRunner input_runner = [environment](const std::string& cmd) {
            return environment->execute(cmd, /*timeoutSeconds=*/5);
        };
        tools->registerTool(std::make_unique<InputTool>(input_runner));
        tools->registerTool(std::make_unique<ScreenshotTool>(
            [environment] { return std::filesystem::path(environment->getWorkspace()); }));
    } else {
        std::cerr << "CANH BAO: Environment hien tai khong ho tro GUI "
                     "(supportsGui() == false) - InputTool/ScreenshotTool "
                     "se khong duoc dang ky.\n";
    }

    auto llm = std::make_unique<ColabClient>("http://localhost:11434","gemma4:e4b",0.2f,2048);  // TODO: doi ten class + model that
    auto skills = std::make_unique<SkillLoader>(/* ... nhu cu ... */);
    auto loop_detector = std::make_unique<LoopDetector>(/* ... nhu cu ... */);

    GUIAgentLoop gui_loop(std::move(llm), tools, std::move(skills), std::move(loop_detector));

    Task task;                       // TODO: dien dung field theo Task that (xem trajectory.h)
    task.instruction = argc > 1 ? argv[1]
        : "bạn mở gmail trên google, soạn cho tôi tin nhắn với tiltle là name, nội dung là gui đã xong cho nqkhanh2510@clc.fitus.edu.vn";
    
    task.max_steps = 15;

    Trajectory result = gui_loop.run(task);

    std::cout << "\n=== Ket qua ===\n";
    for (const auto& step : result.steps) {  // TODO: doi ten field theo Trajectory that
        std::cout << "- [" << step.action_type << "] " << step.tool_name << "\n";
    }

    environment->teardown();
    return 0;
}