#pragma once
#include <string>
#include <optional>

struct Step {
    int step_id;
    std::string thought;
    std::string action_type;
    std::string tool_name;
    std::string tool_args;
    std::optional<std::string> tool_result;
    int tokens_used;
    int latency_ms;
};