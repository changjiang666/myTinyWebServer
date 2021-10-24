#include "log/log.hpp"
#include <iostream>
#include <unistd.h>
using namespace std;

int main() {
    // 开启日志功能
    int m_closeLog = 0;
    // 异步写日志 1   同步 0
    int m_logWrite = 1;

    if (1 == m_logWrite) {
        Log::get_instance()->init("./log", m_closeLog, 2000, 800000, 800);
    } else {
        Log::get_instance()->init("./log", m_closeLog, 2000, 800000, 0);
    }

    int a = 1, b = 2;
    LOG_INFO("a = %d", a);
    LOG_INFO("b = %d", b);
    LOG_INFO("a + b = %d", a + b);
    sleep(2);
    return 0;
}