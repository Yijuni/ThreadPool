#pragma once
#include<vector>
#include<condition_variable>
#include<mutex>
#include<queue>
#include<atomic>
#include<memory>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>
#include <iostream>

const int TASK_MAX_THRESHOLD = 2;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_IDLE_MAX_TIME = 60;//线程最大空余时间10s

enum class ThreadPoolMod{
	MODE_FIXED,//固定线程数
	MODE_CACHED,//线程数动态增加
};

//线程类型
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func) :
		func_m(func),
		threadId_m(generateId_m++)
	{
	}
	~Thread() = default;
	//线程启动
	void Start()
	{
		//创建一个线程来执行一个线程函数
		std::thread t(func_m, threadId_m);
		t.detach();//分离线程，防止任务被挂掉
	}
	//获取线程ID
	int GetId() const
	{
		return threadId_m;
	}
private:
	ThreadFunc func_m;
	static int generateId_m;
	int threadId_m;//保存线程ID
};
int Thread::generateId_m = 0;

//线程池
class ThreadPool
{
public:
	ThreadPool() :
		taskSize_m(0),
		taskQueueMaxThreshold_m(TASK_MAX_THRESHOLD),
		initThreadSize_m(4),
		idleThreadSize_m(0),
		curThreadSize_m(0),
		poolMod_m(ThreadPoolMod::MODE_FIXED),
		poolIsRunning_m(false),
		threadMaxThreshold_m(THREAD_MAX_THRESHOLD)
	{}
	~ThreadPool()
	{
		poolIsRunning_m = false;
		std::unique_lock<std::mutex> lock(taskQueueMtx_m);
		notEmptyCond_m.notify_all();
		exitCond_m.wait(lock, [&]()->bool {return curThreadSize_m == 0; });
	}
	//设置线程池模式
	void SetMode(ThreadPoolMod mode)
	{
		if (CheckRunningState()) return;
		poolMod_m = mode;
	}
	//启用线程池,默认值系统CPU核心数
	void Start(int initThreadSize=std::thread::hardware_concurrency())
	{
		//记录初始线程数量
		initThreadSize_m = initThreadSize;
		poolIsRunning_m = true;

		//创建线程对象
		for (int i = 0; i < initThreadSize_m; i++) {
			//创建thread线程对象的时候，把线程池的函数给到thread线程对象,这样线程对象执行的函数就可以访问线程池内的资源（任务）
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
			int threadId = ptr->GetId();
			threadPool_m.emplace(threadId, std::move(ptr));//emplace_back会调用unique_ptr的拷贝构造，但是unique_ptr不允许拷贝构造，只支持移动构造
		}
		//启动所有线程
		for (auto iter = threadPool_m.begin(); iter != threadPool_m.end(); iter++) {
			iter->second->Start();
			idleThreadSize_m++;
			curThreadSize_m++;
		}

	}
	//设置任务队列数量上限阈值
	void SetThreadMaxThreshold(int threshold)
	{
		if (CheckRunningState()) return;
		if (poolMod_m == ThreadPoolMod::MODE_CACHED) {
			threadMaxThreshold_m = threshold;
		}
	}
	//给线程池提交任务
	//使用可变参模板编程，让SubmitTask可以接受任意函数和参数
	//右值引用和引用折叠
	//函数返回值需要future，future<任务函数返回值类型> ，任务函数返回值类型让其自动推导
	template<typename Func,typename... Args>
	auto SubmitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>//指定返回值类型
	{
		//打包任务，放入任务队列
		using Rtype = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<Rtype()>>(
			std::bind(std::forward< Func>(func), std::forward<Args>(args)...)//forward保持原本性质
		);
		std::future<Rtype> result = task->get_future();
		//获取锁
		std::unique_lock<std::mutex> lock(taskQueueMtx_m);
		//线程通信 等待任务队列有空间
		//先获取锁，再判断条件满不满足，不满足直接阻塞并且释放锁
		//用户提交任务，最长阻塞时间不能超1s，否则判断任务提交失败
		bool  flag = notFullCond_m.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQueue_m.size() < taskQueueMaxThreshold_m; });
		if (!flag) {
			//等待了1s，队列仍然是满的，那就任务提交失败
			std::cerr << "task queue is full ,submit task fail." << std::endl;
			auto taskFail = std::make_shared<std::packaged_task<Rtype()>>( []()->Rtype {  return Rtype(); } );//给个空任务代表返回失败
			(*taskFail)();
			return taskFail->get_future();
		}
		//任务放进任务队列中
		//因为任务队列不可能知道任意函数的返回值和参数，所以加了个中间函数，这样任务队列的任务就能统一
		//放入一个无返回值无参数的函数对象，函数对象内再执行package_task任务,然后进而执行用户传入的func
		taskQueue_m.emplace([task]() {
			//去执行下面任务
			(*task)();//解引用获得package_task<Rtype()>,package_task重载了()表示执行函数
			});
		taskSize_m++;
		//通知消费者线程有任务快去执行
		notEmptyCond_m.notify_all();

		//cached 任务处理比较紧急，小而快的任务 根据任务数量和空闲线程数量，判断是否需要扩展线程,总线程数不能超过阈值
		if (poolMod_m == ThreadPoolMod::MODE_CACHED && taskSize_m > idleThreadSize_m && curThreadSize_m < threadMaxThreshold_m) {
			//创建新线程
			std::cout << ">>>creat new thread ..." << std::endl;
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
			int threadId = ptr->GetId();
			threadPool_m.emplace(threadId, std::move(ptr));
			threadPool_m[threadId]->Start();
			curThreadSize_m++;
			idleThreadSize_m++;
		}

		return result;
	}
	//设置线程池cached模型下线程数阈值
	void SetTaskQueueMaxThreshold(int threshold)
	{
		if (CheckRunningState()) return;
		taskQueueMaxThreshold_m = threshold;
	}
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//定义线程函数,运行在不同的线程中，来从任务队列里取任务执行
	void ThreadFunc(int threadId)//传入线程id，方便后序删除线程
	{
		auto lastRunTime = std::chrono::high_resolution_clock().now();//高精度时间
		std::cout << "begin threadFunc t_id=" << std::this_thread::get_id() << std::endl;
		//所有任务执行完才退出线程
		while (true) {
			Task task;
			{//获取完任务之后，确保锁自动释放，让其他线程使用，本线程执行取到的任务(否则同一时刻只有一个线程在执行任务）
				//先获取锁
				std::unique_lock<std::mutex> lock(taskQueueMtx_m);
				std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务!" << std::endl;
				//cached模式下，创建太多线程但是线程空闲时间超过60s（也就是60s没任务获取）就应该把多余线程删除
				//多余的线程就是指超过initThreadSize_m的线程数
				//当前时间 - 上一次执行的时间 > 60s
					//每一秒钟返回一次
				while (taskSize_m <= 0) {//任务数量小于等于0,就等待，防止虚假唤醒
					if (!poolIsRunning_m) {//没任务并且线程池要退出，那就释放线程
						//回收线程
						threadPool_m.erase(threadId);
						curThreadSize_m--;
						idleThreadSize_m--;
						std::cout << "thread id : " << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_m.notify_all();
						return;
					}
					if (poolMod_m == ThreadPoolMod::MODE_CACHED) {
						if (std::cv_status::timeout == notEmptyCond_m.wait_for(lock, std::chrono::seconds(1))) {//等待超时1s
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastRunTime);//指定返回的时间精确度，精确到秒还是毫秒
							if (dur.count() >= THREAD_IDLE_MAX_TIME && curThreadSize_m > initThreadSize_m) {
								//回收线程
								//不好确定，本线程对应于线程池哪个线程，从而进行移除
								//thread_id -> 线程对象 -> 线程池内删除
								threadPool_m.erase(threadId);
								curThreadSize_m--;
								idleThreadSize_m--;
								std::cout << "thread id : " << std::this_thread::get_id() << "exit!" << std::endl;
								exitCond_m.notify_all();
								return;//函数执行完返回线程会自己释放
							}
						}
					}
					else
					{
						//等待notEmpty条件变量
						notEmptyCond_m.wait(lock);
					}
				}
				idleThreadSize_m--;//有任务要处理了，所以空闲线程数减小
				//取一个任务执行
				task = taskQueue_m.front();
				taskQueue_m.pop();
				taskSize_m--;
				if (taskQueue_m.size() > 0) {
					//告诉其他消费者线程，还有任务存在
					notEmptyCond_m.notify_all();
				}
				//通知生产者线程队列有空闲
				notFullCond_m.notify_all();
			}//unique_lock作用结束

			if (task != nullptr) {
				std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功" << std::endl;
				task();//执行用户提交任务并处理Result
				//任务执行完，通知
			}
			idleThreadSize_m++;//任务处理完空闲线程数增加
			lastRunTime = std::chrono::high_resolution_clock().now();//更新任务执行完的时间
		}

	}
	//检查pool的运行状态
	bool CheckRunningState() const
	{
		return poolIsRunning_m;
	}

	//std::vector<std::unique_ptr<Thread>> threadPool_m;//线程池
	std::unordered_map<int, std::unique_ptr<Thread>> threadPool_m;
	size_t initThreadSize_m;//初始线程数
	ThreadPoolMod poolMod_m;
	size_t threadMaxThreshold_m;//线程数量上限阈值
	//记录空闲线程数
	std::atomic_uint idleThreadSize_m;
	//记录线程池里线程总数量
	std::atomic_int curThreadSize_m;
	//记录线程池是否启动
	std::atomic_bool poolIsRunning_m;

	//Task -> 任务对象
	using Task = std::function<void()>;
	std::queue<Task> taskQueue_m; //任务队列
	std::atomic_uint taskSize_m; //任务数量(原子类型，该类型的数据修改都是原子操作）
	size_t taskQueueMaxThreshold_m; //任务数量上限阈值
	std::mutex taskQueueMtx_m; //任务队列的线程安全
	std::condition_variable notFullCond_m; //任务队列不满
	std::condition_variable notEmptyCond_m; //任务队列不空	
	std::condition_variable exitCond_m;//等待线程资源全部回收

};
