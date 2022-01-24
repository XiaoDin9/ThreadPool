/*
 * @Author: your name
 * @Date: 2022-01-24 04:14:35
 * @LastEditTime: 2022-01-24 06:19:22
 * @LastEditors: Please set LastEditors
 * @Description: 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 * @FilePath: /ThreadPool/ThreadPool.h
 */
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;
    
    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)
{
    // 创建线程、并绑定入口函数（lamda）、开始执行入口函数
    for(size_t i = 0;i<threads;++i)
        // 知识点1：emplace_back 比 push_back 的性能更好，减少临时 copy 的消耗， Q.??? 查明一下具体的原因是啥
        workers.emplace_back(
            // 知识点2：lamda 值传递和引用传递， Q.??? 如何实现
            [this]
            {
                for(;;)
                {
                    // 知识点3： 通过std::function对C++中各种可调用实体（普通函数、Lambda表达式、函数指针、以及其它函数对象等）的封装，
                    // 形成一个新的可调用的std::function对象；让我们不再纠结那么多的可调用实体。
                    std::function<void()> task;            
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);           // 形成互斥区
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });      // 条件变量是保证获取task的同步性: 一个empty 的队列，线程应该等待（阻塞）

                        if(this->stop && this->tasks.empty())
                            return;
                            
                        // 知识点4：std::move(.) 函数的使用，Q.??? 如何使用    
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                }
            }
        );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

#endif
