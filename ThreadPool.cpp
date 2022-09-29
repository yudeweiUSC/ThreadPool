#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <iostream>
#include <chrono>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t);    
    template<class T, class... Args>
    auto post(T&&, Args&&... args) 
        -> std::future<typename std::result_of<T(Args...)>::type>;
    ~ThreadPool();

private:
    std::vector<std::thread> workers;               // All threads in the thread pool
    std::queue<std::function<void()>> tasks;        // queue of pending tasks
    std::mutex queue_mutex;                         // Access to the mutex of the task queue
    std::condition_variable condition;              // Conditional locks for sleeping and waking idle threads
    std::atomic<bool> stop = { false };             // Flag used to determine whether to terminate the thread
};

ThreadPool::ThreadPool(size_t pool_size) {
    for (size_t i = 0; i < pool_size; ++i) workers.emplace_back([this] {        // Create pool_size threads
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex);                     // Attempt to acquire mutex, otherwise block
            if (tasks.empty()) condition.wait(lock, [this] {                    // If the task queue is empty, wait until woken up
                return stop || !tasks.empty();                                  // If it returns false, continue to sleep
            });
            if (stop && tasks.empty()) return;                                  // If stop and the task queue is empty, exit the thread
            auto&& task = std::function<void()>(std::move(tasks.front()));      // Get the next task in the task queue
            tasks.pop();                                                        // and remove this task from the task queue
            lock.unlock();                                                      // release mutex
            task();                                                             // Execute the acquired task
        }
    });
}

template<class T, class... Args>
auto ThreadPool::post(T&& task, Args&&... args) 
    -> std::future<typename std::result_of<T(Args...)>::type> {
    using return_type = typename std::result_of<T(Args...)>::type;
    auto _task = std::make_shared<std::packaged_task<return_type()>>(           // Encapsulate the task to be executed as packaged_task
        std::bind(task, std::forward<Args>(args)...)
    );
    auto res = _task->get_future();                                             // Get the running result of packaged_task for return
    {
        std::unique_lock<std::mutex> lock(queue_mutex);                         // Attempt to acquire mutex, otherwise block
        tasks.emplace([_task] { (*_task)(); });                                 // Put the packaged task into the task queue
    }
    condition.notify_all();                                                     // Wake up threads in the thread pool to perform tasks
    return res;
}

ThreadPool::~ThreadPool() {
    stop = true;
    condition.notify_all();                         // wake up all threads so they can terminate themselves
    for (auto&& worker : workers) worker.join();    // wait for all threads to exit
}

int main() {
    ThreadPool pool(3);                             // Set the number of threads in the thread pool
    std::future<int> result = pool.post([&]() {     // std::future<int> result = std::async(std::launch::async, [&]() {
        std::vector<std::future<int>> results;
        auto task = [](int i) {
            std::cout << "start task " + std::to_string(i) + "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 200));
            std::cout << "end task " + std::to_string(i) + "\n";
            return i * i;
        };
        for (int i = 0; i < 10; ++i) results.emplace_back(pool.post(task, i));  // Because the post task is inside the post task again
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));          // There are at least 2 threads in the thread pool to execute
        for (int i = 10; i < 16; ++i) results.emplace_back(pool.post([i] {
            std::cout << "start task " + std::to_string(i) + "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 150));
            std::cout << "end task " + std::to_string(i) + "\n";
            return i * i;
        }));
        for (auto&& result : results) result.wait();
        for (auto&& result : results) std::cout << std::to_string(result.get()) + "\n";
        return 0;
    });
    std::cout << "waiting for result\n";
    std::cout << std::to_string(result.get()) + "\n";
    return 0;
}
