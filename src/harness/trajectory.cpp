#include "trajectory.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string Trajectory::exportToJson() const {
    json j_steps = json::array();
    for (const auto& s : steps) {
        j_steps.push_back({
            {"step_id", s.step_id},
            {"thought", s.thought},
            {"action", {
                {"type", s.action_type},
                {"tool", s.tool_name},
                {"args", s.tool_args}
            }},
            // [SỬA]: nlohmann::json không có to_json built-in cho std::optional<T>,
            // truyền thẳng s.tool_result (std::optional<std::string>) vào
            // initializer_list_t làm push_back() fail overload resolution toàn
            // bộ object -> phải tự chuyển optional -> json null/value ở đây.
            {"tool_result", s.tool_result ? json(*s.tool_result) : json(nullptr)},
            {"tokens_used", s.tokens_used},
            {"latency_ms", s.latency_ms}
        });
    }

    json j_traj = {
        {"task_id", task_id},
        {"model", model},
        {"success", success},
        {"total_tokens", total_tokens},       // [SỬA]: "total tokens" -> "total_tokens"
        {"total_time_ms", total_time_ms},     // [SỬA]: "total time ms" -> "total_time_ms"
        {"steps", j_steps}
    };

    return j_traj.dump(4);
}