cmake_minimum_required(VERSION 3.10)

project(s3_project)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 헤더 파일 경로 추가
include_directories(include)

# UART 기반 Pub/Sub 시스템 (인터넷 불필요)
add_executable(uart_pubsub
    src/uart_pubsub_main.cpp
    src/uart_broker_impl.cpp
    src/uart_message_impl.cpp
    src/uart_comm_impl.cpp
)

# pthread 링크
target_link_libraries(uart_pubsub pthread)
