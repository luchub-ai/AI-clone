#pragma once
#include "embedding_client.h"
#include <string>
#include <vector>
#include <optional>

class OllamaEmbeddingClient : public EmbeddingClient {
private:
    std::string base_url_;
    std::string model_name_;

    void handleError(const std::string& error_type, const std::string& raw_buffer);

public:
    // Mac dinh khop README.md: Ollama chay cuc bo tai localhost:11434.
    // Model mac dinh "nomic-embed-text" - can `ollama pull nomic-embed-text`
    // truoc khi dung that (giong tinh than OllamaClient::chat() can
    // `ollama pull <model>` truoc, xem comment dau benchmark/run_eval.cpp).
    explicit OllamaEmbeddingClient(std::string base_url = "http://localhost:11434",
                                    std::string model = "nomic-embed-text");

    std::optional<std::vector<float>> embed(const std::string& text) override;
};
