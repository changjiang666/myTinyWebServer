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


add_executable(test_threadPool test/test_threadPool.cc src/lock/locker.cc src/mysql/sqlConnectionPool.cc)
target_link_libraries(test_threadPool ${MYSQL_LIBS})