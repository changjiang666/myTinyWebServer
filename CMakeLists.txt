cmake_minimum_required(VERSION 3.5)
project(myTinyWebServer)

set(CMAKE_CXX_STANDARD 11)
# 使用多线程
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-Wall -g -O0")

include_directories(${CMAKE_SOURCE_DIR}/include)

# 链接mysql库
include_directories("/usr/include/mysql")
link_libraries("/usr/lib/x86_64-linux-gnu")
set(MYSQL_LIBS mysqlclient)

FILE(GLOB SRC_FILES "./src/config/*.cc"
                    "./src/http/*.cc"
                    "./src/lock/*.cc"
                    "./src/mysql/*.cc"
                    "./src/log/*.cc"
                    # "./src/threadpool/*.cc"
                    "./src/timer/*.cc"
                    "./src/webServer/*.cc")

add_executable(server ./main.cc ${SRC_FILES})
target_link_libraries(server ${MYSQL_LIBS})