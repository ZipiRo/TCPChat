#pragma once

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <iostream>
#include <atomic>

class Logger
{
private:
    std::thread debug_thread;
    std::queue<std::string> debug_queue;

    std::mutex debug_mutex;
    std::condition_variable debug_cv;

    std::atomic<bool> debug_running;

    Logger() 
    {
        debug_running = true;
        debug_thread = std::thread(&Logger::DebugThread, this);
    }

    ~Logger()
    {
        CloseLogger();
    }

    void DebugThread()
    {
        std::unique_lock<std::mutex> lock(debug_mutex);

        while(debug_running)
        {
            debug_cv.wait(lock, [this] {
                return !debug_queue.empty() || !debug_running;
            });

            while(!debug_queue.empty())
            {
                std::string log = std::move(debug_queue.front());
                debug_queue.pop();

                lock.unlock();

                std::cout << "> " << log << '\n';

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

    static void CloseLogger()
    {
        Logger &logger = Logger::GetInstance();
        
        logger.debug_running = false;
        logger.debug_cv.notify_all();
        if (logger.debug_thread.joinable())
            logger.debug_thread.join();
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