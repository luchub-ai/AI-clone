#pragma once
#include "src/tools/tool.h"


class CalculatorTool : public Tool {
public:
    [[nodiscard]] std::string getName() const override;

    [[nodiscard]] std::string getDescription() const override;

    std::optional<std::string> execute(const std::string& args) override;
};