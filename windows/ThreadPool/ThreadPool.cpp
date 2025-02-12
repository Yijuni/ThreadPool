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


Result ThreadPool::SubmitTask(std::shared_ptr<Task> task)
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
		return Result(task,false);
	}
	//任务放进任务队列中
	taskQueue_m.emplace(task);
	taskSize_m++;
	//通知消费者线程有任务快去执行
	notEmptyCond_m.notify_all();
	
	return Result(task);
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
			task->exec();//执行用户提交任务并处理Result
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


Semaphore::Semaphore(int source) :source_m(source)
{
}

Semaphore::~Semaphore()
{
}

void Semaphore::wait()
{
	std::unique_lock<std::mutex> lock(mutex_m);
	cond_m.wait(lock, [&]()->bool {return source_m > 0; });
	source_m--;
}

void Semaphore::post()
{
	std::unique_lock<std::mutex> lock(mutex_m);
	source_m++;
	cond_m.notify_all();
}

Result::Result()
{
}

Result::Result(std::shared_ptr<Task> task,bool isValid):task_m(task),isValid_m(isValid)
{
	task->setResult(this);
}

Result::~Result()
{
}

void Result::SetVal(Any res)
{
	any_m = std::move(res);//任务执行完把结果给Result内的any_m
	sem_m.post();//任务执行完，会调用这个函数，信号量+1(source=1)
}

Any Result::Get()
{
	if (!isValid_m) {
		return "";
	}
	sem_m.wait();//任务没执行完，这个位置会阻塞(source_m=0)
	return std::move(any_m);
}

Task::Task():res_m(nullptr)
{
}

void Task::exec()
{
	if (res_m == nullptr) return;
	res_m->SetVal(Run());//这里发生多态调用，这样就可以帮用户执行任务，而且还能进行其他处理（告诉Result执行完了)
}

void Task::setResult(Result* res)
{
	res_m = res;
}
