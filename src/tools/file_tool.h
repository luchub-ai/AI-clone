#pragma once
#include "src/tools/tool.h"
#include "src/harness/environment/environment.h"
#include <cstddef>
#include <optional>
#include <string>

// FileTool: uy quyen toan bo thao tac file cho Environment. FileTool chi:
//   1) Parse JSON args
//   2) Ap policy rieng (gioi han kich thuoc content)
//   3) Format ket qua/loi cho LLM doc
//
// Args: {"action": "read|write|append|mkdir|list|delete", "path": "...", "content": "..."}
class FileTool : public Tool {
public:
    explicit FileTool(Environment* env, std::size_t maxContentBytes = 5 * 1024 * 1024);

    [[nodiscard]] std::string getName() const override;
    [[nodiscard]] std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;

private:
    Environment* env_;
    std::size_t max_content_bytes_;
};