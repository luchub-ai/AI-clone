#pragma once
#include "tool.h"
#include <filesystem>
#include <optional>
#include <functional>
#include <string>

class ScreenshotTool : public Tool {
public:
    using WorkspaceProvider = std::function<std::filesystem::path()>;

    explicit ScreenshotTool(WorkspaceProvider workspace_provider);

    std::string getName() const override;
    std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;

private:
    WorkspaceProvider workspace_provider_; 

    std::filesystem::path generateOutputPath() const;
    bool callPortalScreenshot(std::string& uri_out, std::string& error_out) const;
};