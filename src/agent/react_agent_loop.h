#pragma once
#include "src/agent/agent_loop.h"

// Cài đặt cụ thể của vòng lặp ReAct thuần text:
//   Thought: ... / Action: tool / Action Input: args
//   Thought: ... / Final Answer: ...
class ReActAgentLoop : public AgentLoop {
public:
    using AgentLoop::AgentLoop;  // kế thừa constructor của AgentLoop

protected:
    void        observe() override;
    std::string think()   override;
    Step         act()    override;
};