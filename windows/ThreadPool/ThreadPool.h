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
#include <unordered_map>

//Any类型可接受任意类型
class Any {
public:
	Any() = default;
	~Any() = default;
	//因为类内有unique_ptr不支持拷贝构造只支持移动构造，所以要右值引用
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data):base_m(std::make_unique<Derive<T>>(data))
	{}

	//这个方法能把Any对象里面存储的数据提取出
	template<typename T>
	T Cast_() {
		//从base_m获取派生类对象内的data_m成员变量
		//基类指针=》派生类指针           RTTI类型识别，会检测是否可以转换指针,如果不能转换就返回nullptr
		//比如 base_m指向的实际上是Derive<int>对象，但是你传入的T是long,Derive<int> -> Derive<long>是会转换失败
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_m.get());
		if (pd == nullptr) {
			throw "type is incompatiable!";//抛异常
		}
		return pd->data_m;
	}
private:
	//基类类型
	class Base {
	public:
		virtual ~Base() = default;
	};

	//派生类类型
	template<typename T>
	class Derive : public Base {
	public:
		Derive(T data):data_m(data){}
		~Derive() {};
		T data_m;
	};

	std::unique_ptr<Base> base_m;
};

//信号量类型
class Semaphore {
public:
	Semaphore(int source=0);
	~Semaphore();
	//获取一个信号量资源
	void wait();
	//增加一个信号量资源
	void post();
private:
	int source_m;
	std::mutex mutex_m;
	std::condition_variable cond_m;
};


class Task;
//任务完成后的返回值类型
class Result {
public:
	Result();
	Result(std::shared_ptr<Task> task,bool isValid=true);
	~Result();
	void SetVal(Any res);//由task调用
	Any Get();//由用户调用
private:
	Semaphore sem_m;//信号量
	Any any_m;//任务返回值
	std::shared_ptr<Task> task_m;
	std::atomic_bool isValid_m;//返回值是否有效，如果任务提交失败，返回值就是无效的
};

//抽象任务类型
class Task {
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	//用户可自定义任务类型，从Task继承，重写方法
	virtual Any Run() = 0;
private:
	//std::shared_ptr<Result> res_m; 这里不能用强智能指针，交叉引用问题，会导致智能指针无法释放
	Result* res_m; //Result生命周期长于Task
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
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();
	//线程启动
	void Start();
	//获取线程ID
	int GetId() const;
private:
	ThreadFunc func_m;
	static int generateId_m;
	int threadId_m;//保存线程ID
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
	//启用线程池,默认值系统CPU核心数
	void Start(int initThreadSize=std::thread::hardware_concurrency());
	//设置任务队列数量上限阈值
	void SetTaskQueueMaxThreshold(int threshold);
	//给线程池提交任务
	Result SubmitTask(std::shared_ptr<Task> task);
	//设置线程池cached模型下线程数阈值
	void SetThreadMaxThreshold(int threshold);
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//定义线程函数,运行在不同的线程中，来从任务队列里取任务执行
	void ThreadFunc(int threadId);//传入线程id，方便后序删除线程
	//检查pool的运行状态
	bool CheckRunningState() const;

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

	//用智能指针，防止任务还未执行完，任务就被释放了（生命周期延长）
	std::queue<std::shared_ptr<Task>> taskQueue_m; //任务队列
	std::atomic_uint taskSize_m; //任务数量(原子类型，该类型的数据修改都是原子操作）
	size_t taskQueueMaxThreshold_m; //任务数量上限阈值
	std::mutex taskQueueMtx_m; //任务队列的线程安全
	std::condition_variable notFullCond_m; //任务队列不满
	std::condition_variable notEmptyCond_m; //任务队列不空	
	std::condition_variable exitCond_m;//等待线程资源全部回收

};

#endif // ! THREADPOOL_H



