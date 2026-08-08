#pragma once
// Mock server toi gian gia lap Tavily Search API, dung rieng cho test
// WebSearchTool - KHONG goi mang that, KHONG ton credit that. Chua co
// mock nao trong repo truoc do (tests/web_search_tool.cpp cu goi thang
// vao 1 SearXNG that tai localhost:8080); file nay la mock POST dau tien.
//
// Chi ho tro dung 1 request cho moi lan start()/stop() - du dung cho tung
// test case goi WebSearchTool::execute() 1 lan. Dung raw POSIX socket +
// std::thread (khong them thu vien ngoai nao) de khop tinh than "khong
// them dependency khong can thiet" cua du an.
//
// Cach dung:
//   MockTavilyServer server(19191);
//   server.respondWith(200, R"({"results":[...]})");
//   server.start();
//   WebSearchTool tool("dummy-key", "http://127.0.0.1:19191", 5, 5);
//   auto r = tool.execute(R"({"query":"test"})");
//   server.stop();

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

class MockTavilyServer {
public:
    explicit MockTavilyServer(int port) : port_(port) {}
    ~MockTavilyServer() { stop(); }

    MockTavilyServer(const MockTavilyServer&) = delete;
    MockTavilyServer& operator=(const MockTavilyServer&) = delete;

    // Cau hinh HTTP status + body (JSON) se tra ve cho request KE TIEP.
    void respondWith(int status_code, std::string body) {
        status_code_ = status_code;
        body_ = std::move(body);
    }

    // Mo socket, lang nghe tai 127.0.0.1:port_, roi accept + tra response
    // tren 1 thread rieng (khong chan main thread - de main thread goi
    // WebSearchTool::execute() song song, "gap" server o giua).
    bool start() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return false;

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(static_cast<uint16_t>(port_));

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        if (listen(listen_fd_, 1) < 0) {
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        running_ = true;
        worker_ = std::thread([this] { acceptOnce(); });
        return true;
    }

    // Dong listen socket (ep accept() dang cho thoat neu chua co client
    // nao ket noi) va cho thread worker ket thuc.
    void stop() {
        if (!running_.exchange(false)) return;
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (worker_.joinable()) worker_.join();
    }

    // Request tho (header + body) ma server nhan duoc lan gan nhat - de
    // test co the kiem tra minh gui header/body co dung khong, neu can.
    [[nodiscard]] std::string lastRequestRaw() const { return last_request_; }

private:
    void acceptOnce() {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) return;  // listen_fd bi dong boi stop() -> thoat lang le

        // Doc header + body: doc toi khi thay "\r\n\r\n" (het header), tinh
        // Content-Length de biet doc du body chua. Khong ho tro chunked -
        // khong can cho mock (WebSearchTool luon gui Content-Length that).
        std::string raw;
        char buf[4096];
        size_t header_end = std::string::npos;
        long content_length = 0;
        while (true) {
            ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            raw.append(buf, static_cast<size_t>(n));

            if (header_end == std::string::npos) {
                header_end = raw.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    size_t cl_pos = raw.find("Content-Length:");
                    if (cl_pos == std::string::npos) cl_pos = raw.find("content-length:");
                    if (cl_pos != std::string::npos && cl_pos < header_end) {
                        content_length = std::strtol(raw.c_str() + cl_pos + 16, nullptr, 10);
                    }
                }
            }
            if (header_end != std::string::npos) {
                long body_so_far = static_cast<long>(raw.size()) - static_cast<long>(header_end + 4);
                if (body_so_far >= content_length) break;
            }
        }
        last_request_ = raw;

        std::ostringstream response;
        response << "HTTP/1.1 " << status_code_ << " Status\r\n"
                  << "Content-Type: application/json\r\n"
                  << "Content-Length: " << body_.size() << "\r\n"
                  << "Connection: close\r\n\r\n"
                  << body_;
        std::string resp_str = response.str();
        send(client_fd, resp_str.c_str(), resp_str.size(), 0);
        close(client_fd);
    }

    int port_;
    int listen_fd_ = -1;
    std::thread worker_;
    std::atomic<bool> running_{false};
    int status_code_ = 200;
    std::string body_ = "{}";
    std::string last_request_;
};
