#include "src/tools/tool_registry.h"
#include <sstream>
#include <algorithm>
#include <iostream>

void ToolRegistry::registerTool(std::unique_ptr<Tool> tool) {
    if (!tool) return;
    std::string name(tool->getName());
    tools[name] = std::move(tool); // Chuyển quyền sở hữu vào map
}

void ToolRegistry::setAllowedTools(const std::vector<std::string>& allow_list) {
    allowed_tools = allow_list;
}

std::optional<std::string> ToolRegistry::executeTool(std::string_view name, const std::string& args) {
    // 1. Kiểm tra chính sách (Tool Policy Allow/Deny)
    if (!allowed_tools.empty()) {
        auto it = std::find(allowed_tools.begin(), allowed_tools.end(), name);
        if (it == allowed_tools.end()) {
            std::cerr << "[Policy Denied]: LLM cố gọi tool bị cấm -> " << name << "\n";
            return std::nullopt;
        }
    }

    // 2. Kỹ thuật C++20: Dùng .contains() thay vì viết map.find() == map.end() dài dòng
    std::string key_name(name);
    if (!tools.contains(key_name)) {
        std::cerr << "[Registry Error]: Không tìm thấy tool -> " << name << "\n";
        return std::nullopt;
    }

    return tools[key_name]->execute(args);
}

std::string ToolRegistry::getToolDescriptions() const {
    std::ostringstream oss;
    oss << "SYSTEM: Bạn có các công cụ sau. Để dùng, hãy trả về đúng tên và đối số:\n";
    
    // Kỹ thuật C++17: Structured binding unpack thẳng key và value của map
    for (const auto& [name, tool_ptr] : tools) {
        // Nếu tool không nằm trong allow_list thì giấu luôn, không giới thiệu cho LLM
        if (!allowed_tools.empty() && 
            std::find(allowed_tools.begin(), allowed_tools.end(), name) == allowed_tools.end()) {
            continue; 
        }
        oss << "- " << name << ": " << tool_ptr->getDescription() << "\n";
    }
    return oss.str();
}