#pragma once
#include "tool.h"
#include <filesystem>
#include <optional>
#include <functional>
#include <string>

class ScreenshotTool : public Tool {
public:
    using WorkspaceProvider = std::function<std::filesystem::path()>;

    // max_width: neu co gia tri va anh chup rong hon gia tri nay, anh se
    // duoc resize xuong (giu ti le) truoc khi tra path ve cho GUIAgentLoop.
    // std::nullopt = giu nguyen kich thuoc goc (hanh vi cu).
    explicit ScreenshotTool(WorkspaceProvider workspace_provider,
                             std::optional<int> max_width = std::nullopt);

    std::string getName() const override;
    std::string getDescription() const override;
    std::optional<std::string> execute(const std::string& args) override;

    // Ti le anh goc / anh da resize o lan chup gan nhat (>= 1.0).
    // GUIAgentLoop/InputTool can nhan them he so nay vao scale_x/scale_y
    // khi convert toa do model tra ve (tinh tren anh DA resize) sang toa
    // do pixel that tren man hinh, neu khong click se lech.
    double getLastResizeRatio() const { return last_resize_ratio_; }

private:
    WorkspaceProvider workspace_provider_;
    std::optional<int> max_width_;
    double last_resize_ratio_ = 1.0;

    std::filesystem::path generateOutputPath() const;
    bool callPortalScreenshot(std::string& uri_out, std::string& error_out) const;
    // Resize in-place file tai path neu vuot max_width_. Cap nhat
    // last_resize_ratio_. Tra false neu doc/ghi anh loi (giu file goc).
    bool resizeIfNeeded(const std::filesystem::path& path);
};