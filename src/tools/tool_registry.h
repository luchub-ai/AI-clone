#pragma once
#include "tool.h"
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <optional>

class ToolRegistry {
private:
    // Dùng std::less<> (C++14/20) để cho phép tra cứu map bằng std::string_view mà không cần tạo temporary std::string
    std::map<std::string, std::unique_ptr<Tool>, std::less<>> tools;    
    std::vector<std::string> allowed_tools; 

public:
    ToolRegistry() = default;

    void registerTool(std::unique_ptr<Tool> tool);
    void setAllowedTools(const std::vector<std::string>& allow_list);

    std::optional<std::string> executeTool(std::string_view name, const std::string& args);
    [[nodiscard]] std::string getToolDescriptions() const;
};

// setAllowedTools