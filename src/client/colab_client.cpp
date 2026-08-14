#include "src/client/colab_client.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <sstream>
#include <utility>
#include "utils/resolve_imagebase64.h"

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

ColabClient::ColabClient(std::string url, std::string model, float temp, int tokens)
    : base_url(std::move(url)), model_name(std::move(model)), temperature(temp), max_tokens(tokens) {}

std::string ColabClient::getModelName() const {
    return model_name;
}

void ColabClient::handleError(const std::string& error_type, const std::string& raw_buffer) {
    std::cerr << "[ColabClient Error - " << error_type << "]\n";
    if (!raw_buffer.empty()) {
        std::cerr << "Raw response received:\n" << raw_buffer << "\n";
    }
}

LLMResponse ColabClient::chat(const std::vector<Message>& history) {
    LLMResponse response;
    response.model = model_name;

    auto request_start = std::chrono::steady_clock::now();
    auto setElapsedLatency = [&response, request_start]() {
        auto elapsed = std::chrono::steady_clock::now() - request_start;
        response.latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        );
    };

    CURL* curl = curl_easy_init();
    if (!curl) {
        handleError("CURL_INIT_FAILED", "");
        return response;
    }

    std::string readBuffer;
    std::string full_url = this->base_url + "/api/chat";

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    
    // [COLAB SPECIFIC]: Timeout dài cho model Vision trên Colab
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 500L); 

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // [COLAB SPECIFIC]: Vượt tường lửa Ngrok Free
    headers = curl_slist_append(headers, "ngrok-skip-browser-warning: 69420");
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // ===================================================================
    // 1. DỰNG MẢNG "MESSAGES" TỪ HISTORY (ĐÃ TÍCH HỢP SANITIZER)
    // ===================================================================
    json messages_array = json::array();

    for (const auto& msg : history) {
        json json_msg = {
            {"role", msg.role},
            {"content", msg.content}
        };
        
        // Cải tiến: Lọc và chuyển đổi từng phần tử trong mảng ảnh
        if (!msg.images_base64.empty()) {   
            json valid_images = json::array();

            for (const auto& img_item : msg.images_base64) {
                auto resolved_b64 = resolveImageToBase64(img_item);
                
                if (resolved_b64.has_value() && !resolved_b64->empty()) {
                    valid_images.push_back(*resolved_b64);
                }
            }

            if (!valid_images.empty()) {
                json_msg["images"] = valid_images;
            } else {
                std::cout << "[Sanitizer] Toàn bộ ảnh của message bị lỗi. Đã hủy trường 'images'.\n";
            }
        }

        messages_array.push_back(json_msg);
    }

    // ===================================================================
    // 2. NẠP BODY CHUẨN OLLAMA API 
    // ===================================================================
    json request_body = {
        {"model", this->model_name},
        {"messages", messages_array},

        // [FIX 524]: PHẢI stream:true để Ollama đẩy byte về NGAY khi có
        // token đầu tiên, thay vì buffer toàn bộ response tới khi generate
        // xong. Với stream:false, nếu tổng thời gian generate > 120s thì
        // Cloudflare Proxy Read Timeout sẽ cắt kết nối (HTTP 524) vì không
        // thấy byte nào chảy qua - dù origin server vẫn đang chạy bình
        // thường. stream:true không làm model generate nhanh hơn, nhưng
        // giữ connection "sống" trong mắt Cloudflare.
        {"stream", true},
        
        // [COLAB SPECIFIC]: Giữ mô hình trên VRAM vĩnh viễn
        {"keep_alive", -1}, 
        
        {"options", {
            {"temperature", this->temperature},
            {"num_predict", this->max_tokens},
            {"num_ctx", this->max_tokens * 4} // [CẬP NHẬT TỪ OLLAMA_CLIENT]
        }}
    };

    std::string json_str = request_body.dump();
    
    // Bật lên nếu bạn cần debug xem cấu trúc JSON truyền đi
    // std::cout << "===== debug colab client =====\n " << request_body.dump(4) << std::endl;
    
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);

    // [DEBUG ROOT-CAUSE 524]: breakdown thời gian THẬT của request này -
    // để biết 125s đang nằm ở giai đoạn nào: DNS / TCP connect / TLS
    // handshake / upload xong request / hay chờ byte đầu tiên từ server.
    // Xoá khối này sau khi xác định xong nguyên nhân.
    {
        double t_dns = 0, t_connect = 0, t_tls = 0, t_pretransfer = 0, t_starttransfer = 0, t_total = 0;
        curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &t_dns);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &t_connect);
        curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &t_tls);
        curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME, &t_pretransfer);
        curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &t_starttransfer);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &t_total);
        std::cerr << "[TIMING] DNS=" << t_dns << "s connect=" << t_connect
                  << "s TLS=" << t_tls << "s request_sent_at=" << t_pretransfer
                  << "s first_byte_at=" << t_starttransfer
                  << "s total=" << t_total
                  << "s | upload_phase=" << (t_pretransfer - t_tls)
                  << "s wait_for_response=" << (t_starttransfer - t_pretransfer) << "s\n";
    }

    if (res != CURLE_OK) {
        std::string curl_err = curl_easy_strerror(res);
        handleError("NETWORK_ERROR (" + curl_err + ")", "");
        setElapsedLatency();
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        handleError("HTTP_STATUS_" + std::to_string(http_code), readBuffer);
        setElapsedLatency();
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // ===================================================================
    // 3. PARSE NDJSON STREAM: stream:true nghĩa là Ollama trả về NHIỀU
    //    dòng JSON riêng biệt (mỗi dòng 1 delta token), KHÔNG phải 1 JSON
    //    object duy nhất như trước -> phải tách dòng rồi ghép content lại,
    //    lấy stats (tokens/duration) từ dòng cuối có "done": true.
    // ===================================================================
    setElapsedLatency();

    std::string full_content;
    bool got_done_line = false;
    std::istringstream stream_buf(readBuffer);
    std::string line;

    try {
        while (std::getline(stream_buf, line)) {
            if (line.empty()) continue;

            json chunk = json::parse(line);

            if (chunk.contains("message") && chunk["message"].contains("content")) {
                full_content += chunk["message"]["content"].get<std::string>();
            }

            if (chunk.value("done", false)) {
                got_done_line = true;
                response.model = chunk.value("model", this->model_name);
                response.tokens_used = chunk.value("prompt_eval_count", 0)
                                     + chunk.value("eval_count", 0);

                long long total_duration_ns = chunk.value("total_duration", 0LL);
                if (total_duration_ns > 0) {
                    response.latency_ms = static_cast<int>(total_duration_ns / 1000000LL);
                }
            }
        }

        if (!got_done_line) {
            // Bytes chảy về nhưng bị cắt giữa chừng (connection drop, v.v.)
            // trước khi thấy dòng "done":true - vẫn coi là lỗi.
            handleError("STREAM_INCOMPLETE_NO_DONE_LINE", readBuffer);
            return response;
        }

        response.content = full_content;
        return response;
    }
    catch (const json::parse_error& e) {
        handleError("MALFORMED_JSON_PARSING_FAILED", readBuffer);
        return response;
    }
}