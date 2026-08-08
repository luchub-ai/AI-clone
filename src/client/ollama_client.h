#pragma once
#include "llm_client.h"
#include <string>
#include <vector>

class OllamaClient : public LLMClient  {
private:
    std::string base_url;
    std::string model_name;
    float temperature;
    int max_tokens;

    void handleError(const std::string& error_type, const std::string& raw_buffer);

public:
    OllamaClient(std::string url, std::string model, float temp, int tokens);

    // [THAY ĐỔI THEO UML]: Cập nhật prototype trùng khớp với lớp cha
    LLMResponse chat(const std::vector<Message>& history) override;

    std::string getModelName() const override;
};