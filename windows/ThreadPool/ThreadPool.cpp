#include "ThreadPool.h"
#include <iostream>
const int TASK_MAX_THRESHOLD = 1024;

ThreadPool::ThreadPool() :
	taskSize_m(0),
	taskQueueMaxThreshold_m(TASK_MAX_THRESHOLD),
	initThreadSize_m(4),
	poolMod_m(ThreadPoolMod::MODE_FIXED)
{

}

ThreadPool::~ThreadPool()
{
	
}

void ThreadPool::SetMode(ThreadPoolMod mode)
{
	poolMod_m = mode;
}

void ThreadPool::Start(int initThreadSize)
{
	//记录初始线程数量
	initThreadSize_m = initThreadSize;

	//创建线程对象
	for (int i = 0; i < initThreadSize_m; i++) {
		//创建thread线程对象的时候，把线程池的函数给到thread线程对象,这样线程对象执行的函数就可以访问线程池内的资源（任务）
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this));
		threadPool_m.emplace_back(std::move(ptr));//emplace_back会调用unique_ptr的拷贝构造，但是unique_ptr不允许拷贝构造，只支持移动构造
	}
	//启动所有线程
	for (int i = 0; i < initThreadSize_m; i++) {
		threadPool_m[i]->Start();
	}
}

void ThreadPool::SetTaskQueueMaxThreshold(int threshold)
{
	taskQueueMaxThreshold_m = threshold;
}


void ThreadPool::SubmitTask(std::shared_ptr<Task> task)
{
	
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueueMtx_m);
	//线程通信 等待任务队列有空间
	//while (taskQueue_m.size() == taskQueueMaxThreshold_m) {
	//	notFullCond_m.wait(lock);
	//}
	//先获取锁，再判断条件满不满足，不满足直接阻塞并且释放锁
	//用户提交任务，最长阻塞时间不能超1s，否则判断任务提交失败
	bool  flag = notFullCond_m.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQueue_m.size() < taskQueueMaxThreshold_m; });
	if (!flag) {
		//等待了1s，队列仍然是满的，那就任务提交失败
		std::cerr << "task queue is full ,submit task fail." << std::endl;
		return;
	}
	//任务放进任务队列中
	taskQueue_m.emplace(task);
	taskSize_m++;
	//通知消费者线程有任务快去执行
	notEmptyCond_m.notify_all();
}


void ThreadPool::ThreadFunc()
{
	std::cout << "begin threadFunc t_id="<< std::this_thread::get_id() << std::endl;
	while(true) {
		std::shared_ptr<Task> task;
		{//获取完任务之后，确保锁自动释放，让其他线程使用，本线程执行取到的任务(否则同一时刻只有一个线程在执行任务）
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueueMtx_m);
			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务!" << std::endl;
			//等待notEmpty条件变量
			notEmptyCond_m.wait(lock, [&]()->bool {return taskSize_m > 0; });
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
		}

		if (task != nullptr) {
			std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功" << std::endl;
			task->Run();//多态执行用户自定义的任务
			//任务执行完，通知
		}
	}
}

Thread::Thread()
{
}

Thread::Thread(ThreadFunc func):
	func_m(func)
{
}

Thread::~Thread()
{

}

void Thread::Start()
{
	//创建一个线程来执行一个线程函数
	std::thread t(func_m);
	t.detach();//分离线程，防止任务被挂掉

}
