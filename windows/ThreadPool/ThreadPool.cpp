#include "ThreadPool.h"
#include <iostream>
const int TASK_MAX_THRESHOLD = INT32_MAX;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_IDLE_MAX_TIME = 60;//线程最大空余时间10s

ThreadPool::ThreadPool() :
	taskSize_m(0),
	taskQueueMaxThreshold_m(TASK_MAX_THRESHOLD),
	initThreadSize_m(4),
	idleThreadSize_m(0),
	curThreadSize_m(0),
	poolMod_m(ThreadPoolMod::MODE_FIXED),
	poolIsRunning_m(false),
	threadMaxThreshold_m(THREAD_MAX_THRESHOLD)
{

}

ThreadPool::~ThreadPool()
{
	poolIsRunning_m = false;
	std::unique_lock<std::mutex> lock(taskQueueMtx_m);
	notEmptyCond_m.notify_all();
	exitCond_m.wait(lock, [&]()->bool {return curThreadSize_m==0; });
}

void ThreadPool::SetMode(ThreadPoolMod mode)
{
	if (CheckRunningState()) return;
	poolMod_m = mode;
}

void ThreadPool::Start(int initThreadSize)
{
	//记录初始线程数量
	initThreadSize_m = initThreadSize;
	poolIsRunning_m = true;
	
	//创建线程对象
	for (int i = 0; i < initThreadSize_m; i++) {
		//创建thread线程对象的时候，把线程池的函数给到thread线程对象,这样线程对象执行的函数就可以访问线程池内的资源（任务）
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
		int threadId = ptr->GetId();
		threadPool_m.emplace(threadId,std::move(ptr));//emplace_back会调用unique_ptr的拷贝构造，但是unique_ptr不允许拷贝构造，只支持移动构造
	}
	//启动所有线程
	for (auto iter = threadPool_m.begin(); iter != threadPool_m.end();iter++) {
		iter->second->Start();
		idleThreadSize_m++;
		curThreadSize_m++;
	}

}

void ThreadPool::SetTaskQueueMaxThreshold(int threshold)
{
	if (CheckRunningState()) return;
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

	//cached 任务处理比较紧急，小而快的任务 根据任务数量和空闲线程数量，判断是否需要扩展线程,总线程数不能超过阈值
	if (poolMod_m == ThreadPoolMod::MODE_CACHED && taskSize_m>idleThreadSize_m && curThreadSize_m< threadMaxThreshold_m) {
		//创建新线程
		std::cout << ">>>creat new thread ..." << std::endl;
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this,std::placeholders::_1));
		int threadId = ptr->GetId();
		threadPool_m.emplace(threadId,std::move(ptr));
		threadPool_m[threadId]->Start();
		curThreadSize_m++;
		idleThreadSize_m++;
	}
	
	return Result(task);
}

void ThreadPool::SetThreadMaxThreshold(int threshold)
{
	if (CheckRunningState()) return;
	if (poolMod_m == ThreadPoolMod::MODE_CACHED) {
		threadMaxThreshold_m = threshold;
	}
}


void ThreadPool::ThreadFunc(int threadId)
{
	auto lastRunTime = std::chrono::high_resolution_clock().now();//高精度时间
	std::cout << "begin threadFunc t_id="<< std::this_thread::get_id() << std::endl;
	while(poolIsRunning_m) {
		std::shared_ptr<Task> task;
		{//获取完任务之后，确保锁自动释放，让其他线程使用，本线程执行取到的任务(否则同一时刻只有一个线程在执行任务）
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueueMtx_m);
			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务!" << std::endl;
			//cached模式下，创建太多线程但是线程空闲时间超过60s（也就是60s没任务获取）就应该把多余线程删除
			//多余的线程就是指超过initThreadSize_m的线程数
			//当前时间 - 上一次执行的时间 > 60s
				//每一秒钟返回一次
			while (poolIsRunning_m && taskSize_m <= 0) {//任务数量小于等于0,就等待，防止虚假唤醒
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
				//线程池要退出，回收线程资源了
				//if (!poolIsRunning_m) {
				//	//回收线程
				//	threadPool_m.erase(threadId);
				//	curThreadSize_m--;
				//	idleThreadSize_m--;
				//	std::cout << "thread id : " << std::this_thread::get_id() << "exit!" << std::endl;
				//	exitCond_m.notify_all();
				//	return;
				//}
			}

			//线程池要退出，回收线程资源了
			if (!poolIsRunning_m) {
				break;
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
			task->exec();//执行用户提交任务并处理Result
			//任务执行完，通知
		}
		idleThreadSize_m++;//任务处理完空闲线程数增加
		lastRunTime = std::chrono::high_resolution_clock().now();//更新任务执行完的时间
	}
	//跳出了while循环说明线程池要结束了
	//回收线程
	threadPool_m.erase(threadId);
	curThreadSize_m--;
	idleThreadSize_m--;
	std::cout << "thread id : " << std::this_thread::get_id() << "exit!" << std::endl;
	exitCond_m.notify_all();
	return;
}

bool ThreadPool::CheckRunningState() const
{
	return poolIsRunning_m;
}

int Thread::generateId_m = 0;

Thread::Thread(ThreadFunc func) :
	func_m(func),
	threadId_m(generateId_m++)
{
}

Thread::~Thread()
{

}

void Thread::Start()
{
	//创建一个线程来执行一个线程函数
	std::thread t(func_m,threadId_m);
	t.detach();//分离线程，防止任务被挂掉

}


int Thread::GetId() const
{
	return threadId_m;
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
