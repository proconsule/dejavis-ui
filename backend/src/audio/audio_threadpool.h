
#ifndef DEJAVIS_APP_AUDIO_THREADPOOL_H
#define DEJAVIS_APP_AUDIO_THREADPOOL_H

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

class AudioThreadPool {
public:
    AudioThreadPool(size_t threads) : stop(false) {
        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                for(;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                    {
                        std::lock_guard<std::mutex> lock(this->wait_mutex);
                        completed_tasks++;
                        wait_condition.notify_one();
                    }
                }
            });
    }

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.push(std::move(task));
        }
        condition.notify_one();
    }

    void waitForAll(size_t expected) {
        std::unique_lock<std::mutex> lock(wait_mutex);
        wait_condition.wait(lock, [this, expected] { return completed_tasks >= expected; });
        completed_tasks = 0;
    }

    ~AudioThreadPool() {
        { std::lock_guard<std::mutex> lock(queue_mutex); stop = true; }
        condition.notify_all();
        for(std::thread &worker: workers) worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;

    std::mutex wait_mutex;
    std::condition_variable wait_condition;
    size_t completed_tasks = 0;
    bool stop;
};

#endif