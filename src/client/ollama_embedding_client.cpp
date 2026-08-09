#include "ollama_embedding_client.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <utility>

using json = nlohmann::json;

static size_t EmbedWriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

OllamaEmbeddingClient::OllamaEmbeddingClient(std::string base_url, std::string model)
    : base_url_(std::move(base_url)), model_name_(std::move(model)) {}

void OllamaEmbeddingClient::handleError(const std::string& error_type, const std::string& raw_buffer) {
    std::cerr << "[OllamaEmbeddingClient Error - " << error_type << "]\n";
    if (!raw_buffer.empty()) {
        std::cerr << "Raw response received:\n" << raw_buffer << "\n";
    }
}

std::optional<std::vector<float>> OllamaEmbeddingClient::embed(const std::string& text) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        handleError("CURL_INIT_FAILED", "");
        return std::nullopt;
    }

    std::string readBuffer;
    // QUAN TRONG: endpoint MOI la /api/embed, KHONG phai /api/embeddings
    // (endpoint cu, da deprecated). Format request/response giua 2 endpoint
    // nay khac nhau - xem ghi chu o duoi cho phan response.
    std::string full_url = base_url_ + "/api/embed";

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    // Embedding 1 cau ngan thuong nhanh hon nhieu so voi sinh van ban dai
    // (chat dung timeout 200s trong ollama_client.cpp) - 60s la du du dam
    // bao khong bi false-timeout luc may dang "am" (cold start model).
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    json request_body = {
        {"model", model_name_},
        {"input", text}
    };
    std::string json_str = request_body.dump();

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, EmbedWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        handleError("NETWORK_ERROR (" + std::string(curl_easy_strerror(res)) + ")", "");
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        handleError("HTTP_STATUS_" + std::to_string(http_code), readBuffer);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    try {
        json response_json = json::parse(readBuffer);

        // DIEM DE SAI NHAT (nhu de bai canh bao): field tra ve la
        // "embeddings" - SO NHIEU, mang CUA mang ([[0.1, 0.2, ...]]) NGAY
        // CA KHI chi gui 1 input - khong phai "embedding" so it nhu
        // endpoint /api/embeddings cu. Kiem tra tuong minh tung buoc
        // truoc khi truy cap (giu dung tinh than "Bug A" da sua trong
        // memory_tool.cpp: kiem tra kieu truoc, khong dua vao exception
        // cho loi cau truc du lieu - response tu server ngoai luon phai
        // coi la khong dang tin cho den khi kiem tra xong).
        if (!response_json.contains("embeddings") || !response_json["embeddings"].is_array()
            || response_json["embeddings"].empty() || !response_json["embeddings"][0].is_array()) {
            handleError("MISSING_OR_MALFORMED_EMBEDDINGS_FIELD", readBuffer);
            return std::nullopt;
        }

        std::vector<float> embedding;
        embedding.reserve(response_json["embeddings"][0].size());
        for (const auto& v : response_json["embeddings"][0]) {
            if (!v.is_number()) {
                handleError("EMBEDDING_VECTOR_NON_NUMERIC_ELEMENT", readBuffer);
                return std::nullopt;
            }
            embedding.push_back(v.get<float>());
        }

        if (embedding.empty()) {
            handleError("EMPTY_EMBEDDING_VECTOR", readBuffer);
            return std::nullopt;
        }

        return embedding;
    } catch (const json::exception& e) {
        handleError(std::string("JSON_PARSE_OR_ACCESS_ERROR: ") + e.what(), readBuffer);
        return std::nullopt;
    }
}
