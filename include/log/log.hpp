#ifndef LOG_LOG_HPP_
#define LOG_LOG_HPP_

#include <stdio.h>
#include <string>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdarg.h>
#include <iostream>
#include "lock/locker.hpp"
#include "log/blockQueue.hpp"

class Log {
public:
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }

    static void* flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
    }

    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);


private:
    Log();
    virtual ~Log();
    void * async_write_log() {
        std::string str;
        while (m_log_queue->pop(str)) {
            m_mutex.lock();
            fputs(str.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;
    block_queue<std::string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    Locker m_mutex;
    int m_close_log; //关闭日志
};

#define LOG_DEBUG(format, ...) if(0 == m_closeLog) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_closeLog) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_closeLog) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_closeLog) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#endif