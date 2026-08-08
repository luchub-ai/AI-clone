# AI_Agent

## Thành viên thực hiện

| MSSV | Họ và tên |
|------|------------|
| 25127329 | Mai Trung Hiếu |
| 25127076 | Nguyễn Quốc Khánh |

## Giới thiệu

Dự án minh họa cách kết nối và tương tác với mô hình ngôn ngữ thông qua Ollama API chạy cục bộ trên máy tính.

---

## Yêu cầu hệ thống

Trước khi chạy chương trình, hãy đảm bảo đã cài đặt:

- CMake
- Visual Studio (C++ Development Tools)
- C++ std 26
- Vcpkg
- Ollama

---

## Hướng dẫn chạy chương trình

### 1. Khởi động Ollama

Đảm bảo Ollama đang hoạt động tại:

```text
http://localhost:11434
```

Tải và chạy mô hình Gemma:

```bash
ollama run gemma
```

---

### 2. Cấu hình và build dự án

Mở terminal tại thư mục gốc của dự án và chạy:

```bash
cmake -B build -S .

cmake --build build

cmake --build build -j$(nproc) # nếu mà muốn build nhanh bằng cách sử dụng tất cả tài nguyên cpu
./build/AI_Agent
```

---

### 3. Chạy chương trình

```bash
.\build\Release\OllamaTest.exe
```

---

## Cấu trúc thư mục

```text
.
├── build/
├── src/
├── CMakeLists.txt
├── README.md
└── vcpkg.json
```

---

## Ghi chú

- Ollama phải được khởi động trước khi chạy chương trình.
- Mặc định chương trình kết nối tới API:

```text
http://localhost:11434
```

- Nếu muốn sử dụng mô hình khác:

```bash
ollama run llama3
```

---

## Tài liệu tham khảo

- https://ollama.com
- https://cmake.org
- https://vcpkg.io