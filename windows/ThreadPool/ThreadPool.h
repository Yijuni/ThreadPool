#ifndef  THREADPOOL_H //如果未定义THREADPOOL_H，则下面代码生效
#define THREADPOOL_H //定义一次THREADPOOL_H，防止冲突（防止多次包含这段代码)

#include<vector>
#include<condition_variable>
#include<mutex>
#include<queue>
#include<atomic>
#include<memory>
#include <functional>
#include <thread>
//抽象任务类型
class Task {
public:
	//用户可自定义任务类型，从Task继承，重写方法
	virtual void Run() = 0;
};

//线程池两种模式
enum class ThreadPoolMod{//加class防止枚举项命名冲突
	MODE_FIXED,//固定数量线程，根据内核数确定
	MODE_CACHED,//线程数量可动态增长
};

//线程类型
class Thread
{
public:
	using ThreadFunc = std::function<void()>;
	Thread();
	Thread(ThreadFunc func);
	~Thread();
	//线程启动
	void Start();
private:
	ThreadFunc func_m;
};
/*
	EXAMPLE:
	ThreadPool pool;
	pool.Start(4);

	class MyTask:public Task
	{
	public:
		void Run(){
			//CODE...
		}
	}

	pool.SubmitTask(std::make_shared<MyTask>());
*/
//线程池
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	//设置线程池模式
	void SetMode(ThreadPoolMod mode=ThreadPoolMod::MODE_FIXED);
	//启用线程池
	void Start(int initThreadSize=4);
	//设置任务队列数量上限阈值
	void SetTaskQueueMaxThreshold(int threshold);
	//给线程池提交任务
	void SubmitTask(std::shared_ptr<Task> task);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//定义线程函数,运行在不同的线程中，来从任务队列里取任务执行
	void ThreadFunc();

	std::vector<std::unique_ptr<Thread>> threadPool_m;//线程池
	size_t initThreadSize_m;//初始线程数
	ThreadPoolMod poolMod_m;
	//用智能指针，防止任务还未执行完，任务就被释放了（生命周期延长）
	std::queue<std::shared_ptr<Task>> taskQueue_m; //任务队列
	std::atomic_uint taskSize_m; //任务数量(原子类型，该类型的数据修改都是原子操作）
	size_t taskQueueMaxThreshold_m; //任务数量上限阈值

	std::mutex taskQueueMtx_m; //任务队列的线程安全
	std::condition_variable notFullCond_m; //任务队列不满
	std::condition_variable notEmptyCond_m; //任务队列不空
	

};

#endif // ! THREADPOOL_H



