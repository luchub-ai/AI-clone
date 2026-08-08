#pragma once
#include <string>
#include <map>
#include <filesystem>

class SkillLoader {
private:
    std::filesystem::path skill_dir;
    std::map<std::string, std::string> skills; // Map: Tên file -> Ruột markdown

public:
    explicit SkillLoader(std::filesystem::path dir = "skills");

    void loadSkillsFromDisk();
    [[nodiscard]] std::string selectSkill(const std::string& keyword) const;
};