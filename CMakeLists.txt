cmake_minimum_required(VERSION 3.25) # bữa set 3.17S
project(AI_Agent LANGUAGES CXX)

# Yêu cầu C++26 theo chuẩn đồ án
set(CMAKE_CXX_STANDARD 26) # bữa set 17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tìm các gói thư viện (Lưu ý: Qua Linux xài apt rồi, quên vcpkg đi nhé!)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)  # can cho std::thread trong tests/tavily_mock_server.h
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
    
# ====================================================================
# TUYỆT CHIÊU: GOM TẤT CẢ CÁC FILE .cpp TRONG src/ VÀ CÁC THƯ MỤC CON
# ====================================================================
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS 
                            "${PROJECT_SOURCE_DIR}/src/*.cpp"
                            "${PROJECT_SOURCE_DIR}/utils/*.cpp")

# main.cpp chứa hàm main() của chương trình demo (AI_Agent). Tách riêng
# ra khỏi SOURCE_FILES để dùng lại phần "thư viện" (agent/client/tools/
# harness) cho executable benchmark (run_eval) bên dưới mà không bị đụng
# độ 2 hàm main() trong cùng 1 file thực thi.
list(REMOVE_ITEM SOURCE_FILES "${PROJECT_SOURCE_DIR}/src/main.cpp")

# # ---- Executable 1: AI_Agent (demo/dev, có hàm main() trong src/main.cpp) ----
# add_executable(AI_Agent ${PROJECT_SOURCE_DIR}/src/main.cpp ${SOURCE_FILES})
# target_include_directories(AI_Agent PRIVATE
#     ${PROJECT_SOURCE_DIR}
#     ${PROJECT_SOURCE_DIR}/src
#     ${PROJECT_SOURCE_DIR}/benchmark
#     ${PROJECT_SOURCE_DIR}/utils
#     ${DBUS_INCLUDE_DIRS}
# )
# target_link_libraries(AI_Agent PRIVATE sqlite3 CURL::libcurl nlohmann_json::nlohmann_json ${DBUS_LIBRARIES})

# # ---- Executable 2: run_eval (benchmark, đọc benchmark/tasks.json) ----
add_executable(run_eval ${PROJECT_SOURCE_DIR}/benchmark/run_eval.cpp ${SOURCE_FILES})
target_include_directories(run_eval PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/benchmark
    ${PROJECT_SOURCE_DIR}/utils
    ${DBUS_INCLUDE_DIRS}
)
target_link_libraries(run_eval PRIVATE sqlite3 CURL::libcurl nlohmann_json::nlohmann_json ${DBUS_LIBRARIES} Threads::Threads)

# debug cho t sài (nếu mà m config cmake rồi thì sài chung với t)
# ---- Executable 2: run_eval (benchmark, đọc benchmark/tasks.json) ----
add_executable(AI_Agent ${PROJECT_SOURCE_DIR}/src/main.cpp ${SOURCE_FILES})
target_include_directories(AI_Agent PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/benchmark
    ${PROJECT_SOURCE_DIR}/utils
    ${DBUS_INCLUDE_DIRS}
)

target_link_libraries(AI_Agent PRIVATE sqlite3 CURL::libcurl nlohmann_json::nlohmann_json ${DBUS_LIBRARIES} Threads::Threads)

# ---------- gui agent ---------------
add_executable(GUI_Agent ${PROJECT_SOURCE_DIR}/benchmark_guiagent/run_eval.cpp ${SOURCE_FILES})
target_include_directories(GUI_Agent PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/benchmark
    ${PROJECT_SOURCE_DIR}/utils
    ${DBUS_INCLUDE_DIRS}
)

target_link_libraries(GUI_Agent PRIVATE sqlite3 CURL::libcurl nlohmann_json::nlohmann_json ${DBUS_LIBRARIES} Threads::Threads)

# ---- Executable 3: multi_agent_demo (de bai 10.3 Multi-agent Coordination, +3d) ----
# Dung LAI 100% ${SOURCE_FILES} (da bao gom san src/common/message_queue.cpp va
# src/tools/message_queue_tool.cpp qua GLOB_RECURSE o tren, khong can khai bao
# rieng) + HarnessRunner::runParallel() moi them trong src/harness/ - CHI khac
# AI_Agent o hem main() rieng (benchmark/multi_agent_demo.cpp), giong dung cach
# AI_Agent/run_eval tach nhau da co san phia tren.
add_executable(multi_agent_demo ${PROJECT_SOURCE_DIR}/benchmark/multi_agent_demo.cpp ${SOURCE_FILES})
target_include_directories(multi_agent_demo PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/benchmark
    ${PROJECT_SOURCE_DIR}/utils
    ${DBUS_INCLUDE_DIRS}
)
target_link_libraries(multi_agent_demo PRIVATE sqlite3 CURL::libcurl nlohmann_json::nlohmann_json ${DBUS_LIBRARIES} Threads::Threads)
