#pragma once

#include <fstream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <iostream>
#include <atomic>

enum WriteType
{
    LOGGER_WRITE_NONE,
    LOGGER_WRITE_CONSOLE,
    LOGGER_WRITE_FILE
};

class Logger
{
private:
    std::ofstream log_file_output;
    WriteType write_type;
    std::string log_file_path;

    std::thread debug_thread;
    std::queue<std::string> debug_queue;

    std::mutex debug_mutex;
    std::condition_variable debug_cv;

    std::atomic<bool> logger_running;

    Logger() 
    {
        logger_running = true;
        write_type = LOGGER_WRITE_NONE;
        debug_thread = std::thread(&Logger::DebugThread, this);
    }

    ~Logger()
    {
        logger_running = false;
        debug_cv.notify_all();
        log_file_output.close();

        if (debug_thread.joinable())
            debug_thread.join();
    }

    void DebugThread()
    {
        std::unique_lock<std::mutex> lock(debug_mutex);

        while(logger_running)
        {
            if(write_type == LOGGER_WRITE_FILE && log_file_path.empty()) continue;
            else 
            {
                log_file_output.open(log_file_path, std::ios_base::app);
            }

            debug_cv.wait(lock, [this] {
                return !debug_queue.empty() || !logger_running;
            });

            while(!debug_queue.empty())
            {
                std::string log = std::move(debug_queue.front());
                debug_queue.pop();

                lock.unlock(); 

                switch (write_type)
                {
                case LOGGER_WRITE_NONE:
                    break;
                case LOGGER_WRITE_CONSOLE:
                    std::cout << "> " << log << '\n';
                    break;
                case LOGGER_WRITE_FILE:
                {
                    log_file_output << "> " << log << '\n';
                    break;
                }
                default:
                    break;
                }

                lock.lock();
            }
        }

        while(!debug_queue.empty())
        {
            std::cout << "> " << debug_queue.front() << '\n';
            debug_queue.pop();
        }
    }

    friend void DebugLog(const std::string &log);

public:
    static Logger& GetInstance()
    {
        static Logger instance;
        return instance;
    }

    static void SetWriteType(WriteType type)
    {
        Logger &logger = Logger::GetInstance();
     
        std::unique_lock<std::mutex> lock(logger.debug_mutex);
        logger.write_type = type;
    }

    static void SetLogFilePath(const std::string &path)
    {
        Logger &logger = Logger::GetInstance();
     
        std::unique_lock<std::mutex> lock(logger.debug_mutex);
        logger.log_file_path = path;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

void DebugLog(const std::string &log)
{
    Logger &logger = Logger::GetInstance();

    {
        std::lock_guard<std::mutex> lock(logger.debug_mutex);
        logger.debug_queue.push(log);
    }

    logger.debug_cv.notify_one();
}