#include <cstdlib>
#include <iostream>
#include <memory>

#include "src/client/ollama_client.h"
#include "src/tools/tool_registry.h"
#include "src/tools/file_tool.h"
#include "src/tools/web_search_tool.h"
#include "src/agent/react_agent_loop.h"
#include "src/agent/skill_loader.h"
#include "src/agent/loop_detector.h"
#include "src/harness/environment/environment.h"
#include "src/common/task.h"

int main() {
    // 1. Workspace that: FileTool se doc/ghi trong nay
    NativeEnvironment env("agent_workspace_demo");
    env.setup();

    // 2. LLM Client - CHINH LAI model_name cho dung model ban da `ollama pull`
    //    Kiem tra bang lenh: ollama list
    auto llm = std::make_unique<OllamaClient>(
        "http://localhost:11434",  // base_url mac dinh cua Ollama
        "qwen3:8b",                  // <-- DOI ten model neu can
        0.2f,                      // temperature thap de AI it "sang tao" linh tinh
        2048                       // max_tokens
    );

    // 3. Tool Registry - chi dang ky 2 tool dang can test
    auto tools = std::make_shared<ToolRegistry>();
    tools->registerTool(std::make_unique<FileTool>
        (env.get()));
    // Can bien moi truong TAVILY_API_KEY da set truoc khi chay demo nay
    // (vd `export TAVILY_API_KEY=tvly-...`), khong thi WebSearchTool se
    // tra loi "chua cau hinh" ngay khi agent goi toi.
    const char* tavily_key_env = std::getenv("TAVILY_API_KEY");
    tools->registerTool(std::make_unique<WebSearchTool>(
        tavily_key_env ? tavily_key_env : "", "https://api.tavily.com", 5, 10));

    // 4. Skill + Loop Detector
    auto skills = std::make_unique<SkillLoader>("skills");
    skills->loadSkillsFromDisk();
    auto loop_det = std::make_unique<LoopDetector>(2, 3);

    // 5. AgentLoop (ReAct)
    ReActAgentLoop agent(std::move(llm), tools, std::move(skills), std::move(loop_det));

    // 6. Task: BAT AI phai dung ca 2 tool
    Task task;
    task.id = "demo_web_search_file";
    task.instruction =
        "Tim kiem tren internet thong tin ve chu de 'AI Agent Framework la gi', "
        "sau do tom tat lai trong khoang 2-3 cau bang tieng Viet va luu vao file ket_qua.txt. "
        "Sau khi luu xong, tra loi Final Answer xac nhan da luu thanh cong.";
    task.max_steps = 6;

    // 7. Chay that
    Trajectory traj = agent.run(task);

    std::cout << "\n========================================\n";
    std::cout << "KET QUA: " << (traj.success ? "THANH CONG" : "THAT BAI (het buoc hoac loi)") << "\n";
    std::cout << "So buoc da chay: " << traj.steps.size() << "\n";
    std::cout << "Tong thoi gian: " << traj.total_time_ms << "ms\n";
    std::cout << "========================================\n";

    return 0;
}