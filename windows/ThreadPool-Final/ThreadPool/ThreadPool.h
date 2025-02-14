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
const int THREAD_IDLE_MAX_TIME = 60;//�߳�������ʱ��10s

enum class ThreadPoolMod{
	MODE_FIXED,//�̶��߳���
	MODE_CACHED,//�߳�����̬����
};

//�߳�����
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
	//�߳�����
	void Start()
	{
		//����һ���߳���ִ��һ���̺߳���
		std::thread t(func_m, threadId_m);
		t.detach();//�����̣߳���ֹ���񱻹ҵ�
	}
	//��ȡ�߳�ID
	int GetId() const
	{
		return threadId_m;
	}
private:
	ThreadFunc func_m;
	static int generateId_m;
	int threadId_m;//�����߳�ID
};
int Thread::generateId_m = 0;

//�̳߳�
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
	//�����̳߳�ģʽ
	void SetMode(ThreadPoolMod mode)
	{
		if (CheckRunningState()) return;
		poolMod_m = mode;
	}
	//�����̳߳�,Ĭ��ֵϵͳCPU������
	void Start(int initThreadSize=std::thread::hardware_concurrency())
	{
		//��¼��ʼ�߳�����
		initThreadSize_m = initThreadSize;
		poolIsRunning_m = true;

		//�����̶߳���
		for (int i = 0; i < initThreadSize_m; i++) {
			//����thread�̶߳����ʱ�򣬰��̳߳صĺ�������thread�̶߳���,�����̶߳���ִ�еĺ����Ϳ��Է����̳߳��ڵ���Դ������
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
			int threadId = ptr->GetId();
			threadPool_m.emplace(threadId, std::move(ptr));//emplace_back�����unique_ptr�Ŀ������죬����unique_ptr�����������죬ֻ֧���ƶ�����
		}
		//���������߳�
		for (auto iter = threadPool_m.begin(); iter != threadPool_m.end(); iter++) {
			iter->second->Start();
			idleThreadSize_m++;
			curThreadSize_m++;
		}

	}
	//���������������������ֵ
	void SetThreadMaxThreshold(int threshold)
	{
		if (CheckRunningState()) return;
		if (poolMod_m == ThreadPoolMod::MODE_CACHED) {
			threadMaxThreshold_m = threshold;
		}
	}
	//���̳߳��ύ����
	//ʹ�ÿɱ��ģ���̣���SubmitTask���Խ������⺯���Ͳ���
	//��ֵ���ú������۵�
	//��������ֵ��Ҫfuture��future<����������ֵ����> ������������ֵ���������Զ��Ƶ�
	template<typename Func,typename... Args>
	auto SubmitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>//ָ������ֵ����
	{
		//������񣬷����������
		using Rtype = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<Rtype()>>(
			std::bind(std::forward< Func>(func), std::forward<Args>(args)...)//forward����ԭ������
		);
		std::future<Rtype> result = task->get_future();
		//��ȡ��
		std::unique_lock<std::mutex> lock(taskQueueMtx_m);
		//�߳�ͨ�� �ȴ���������пռ�
		//�Ȼ�ȡ�������ж������������㣬������ֱ�����������ͷ���
		//�û��ύ���������ʱ�䲻�ܳ�1s�������ж������ύʧ��
		bool  flag = notFullCond_m.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQueue_m.size() < taskQueueMaxThreshold_m; });
		if (!flag) {
			//�ȴ���1s��������Ȼ�����ģ��Ǿ������ύʧ��
			std::cerr << "task queue is full ,submit task fail." << std::endl;
			auto taskFail = std::make_shared<std::packaged_task<Rtype()>>( []()->Rtype {  return Rtype(); } );//���������������ʧ��
			(*taskFail)();
			return taskFail->get_future();
		}
		//����Ž����������
		//��Ϊ������в�����֪�����⺯���ķ���ֵ�Ͳ��������Լ��˸��м亯��������������е��������ͳһ
		//����һ���޷���ֵ�޲����ĺ������󣬺�����������ִ��package_task����,Ȼ�����ִ���û������func
		taskQueue_m.emplace([task]() {
			//ȥִ����������
			(*task)();//�����û��package_task<Rtype()>,package_task������()��ʾִ�к���
			});
		taskSize_m++;
		//֪ͨ�������߳��������ȥִ��
		notEmptyCond_m.notify_all();

		//cached ������ȽϽ�����С��������� �������������Ϳ����߳��������ж��Ƿ���Ҫ��չ�߳�,���߳������ܳ�����ֵ
		if (poolMod_m == ThreadPoolMod::MODE_CACHED && taskSize_m > idleThreadSize_m && curThreadSize_m < threadMaxThreshold_m) {
			//�������߳�
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
	//�����̳߳�cachedģ�����߳�����ֵ
	void SetTaskQueueMaxThreshold(int threshold)
	{
		if (CheckRunningState()) return;
		taskQueueMaxThreshold_m = threshold;
	}
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//�����̺߳���,�����ڲ�ͬ���߳��У��������������ȡ����ִ��
	void ThreadFunc(int threadId)//�����߳�id���������ɾ���߳�
	{
		auto lastRunTime = std::chrono::high_resolution_clock().now();//�߾���ʱ��
		std::cout << "begin threadFunc t_id=" << std::this_thread::get_id() << std::endl;
		//��������ִ������˳��߳�
		while (true) {
			Task task;
			{//��ȡ������֮��ȷ�����Զ��ͷţ��������߳�ʹ�ã����߳�ִ��ȡ��������(����ͬһʱ��ֻ��һ���߳���ִ������
				//�Ȼ�ȡ��
				std::unique_lock<std::mutex> lock(taskQueueMtx_m);
				std::cout << "tid: " << std::this_thread::get_id() << "���Ի�ȡ����!" << std::endl;
				//cachedģʽ�£�����̫���̵߳����߳̿���ʱ�䳬��60s��Ҳ����60sû�����ȡ����Ӧ�ðѶ����߳�ɾ��
				//������߳̾���ָ����initThreadSize_m���߳���
				//��ǰʱ�� - ��һ��ִ�е�ʱ�� > 60s
					//ÿһ���ӷ���һ��
				while (taskSize_m <= 0) {//��������С�ڵ���0,�͵ȴ�����ֹ��ٻ���
					if (!poolIsRunning_m) {//û�������̳߳�Ҫ�˳����Ǿ��ͷ��߳�
						//�����߳�
						threadPool_m.erase(threadId);
						curThreadSize_m--;
						idleThreadSize_m--;
						std::cout << "thread id : " << std::this_thread::get_id() << "exit!" << std::endl;
						exitCond_m.notify_all();
						return;
					}
					if (poolMod_m == ThreadPoolMod::MODE_CACHED) {
						if (std::cv_status::timeout == notEmptyCond_m.wait_for(lock, std::chrono::seconds(1))) {//�ȴ���ʱ1s
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastRunTime);//ָ�����ص�ʱ�侫ȷ�ȣ���ȷ���뻹�Ǻ���
							if (dur.count() >= THREAD_IDLE_MAX_TIME && curThreadSize_m > initThreadSize_m) {
								//�����߳�
								//����ȷ�������̶߳�Ӧ���̳߳��ĸ��̣߳��Ӷ������Ƴ�
								//thread_id -> �̶߳��� -> �̳߳���ɾ��
								threadPool_m.erase(threadId);
								curThreadSize_m--;
								idleThreadSize_m--;
								std::cout << "thread id : " << std::this_thread::get_id() << "exit!" << std::endl;
								exitCond_m.notify_all();
								return;//����ִ���귵���̻߳��Լ��ͷ�
							}
						}
					}
					else
					{
						//�ȴ�notEmpty��������
						notEmptyCond_m.wait(lock);
					}
				}
				idleThreadSize_m--;//������Ҫ�����ˣ����Կ����߳�����С
				//ȡһ������ִ��
				task = taskQueue_m.front();
				taskQueue_m.pop();
				taskSize_m--;
				if (taskQueue_m.size() > 0) {
					//���������������̣߳������������
					notEmptyCond_m.notify_all();
				}
				//֪ͨ�������̶߳����п���
				notFullCond_m.notify_all();
			}//unique_lock���ý���

			if (task != nullptr) {
				std::cout << "tid: " << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;
				task();//ִ���û��ύ���񲢴���Result
				//����ִ���֪꣬ͨ
			}
			idleThreadSize_m++;//������������߳�������
			lastRunTime = std::chrono::high_resolution_clock().now();//��������ִ�����ʱ��
		}

	}
	//���pool������״̬
	bool CheckRunningState() const
	{
		return poolIsRunning_m;
	}

	//std::vector<std::unique_ptr<Thread>> threadPool_m;//�̳߳�
	std::unordered_map<int, std::unique_ptr<Thread>> threadPool_m;
	size_t initThreadSize_m;//��ʼ�߳���
	ThreadPoolMod poolMod_m;
	size_t threadMaxThreshold_m;//�߳�����������ֵ
	//��¼�����߳���
	std::atomic_uint idleThreadSize_m;
	//��¼�̳߳����߳�������
	std::atomic_int curThreadSize_m;
	//��¼�̳߳��Ƿ�����
	std::atomic_bool poolIsRunning_m;

	//Task -> �������
	using Task = std::function<void()>;
	std::queue<Task> taskQueue_m; //�������
	std::atomic_uint taskSize_m; //��������(ԭ�����ͣ������͵������޸Ķ���ԭ�Ӳ�����
	size_t taskQueueMaxThreshold_m; //��������������ֵ
	std::mutex taskQueueMtx_m; //������е��̰߳�ȫ
	std::condition_variable notFullCond_m; //������в���
	std::condition_variable notEmptyCond_m; //������в���	
	std::condition_variable exitCond_m;//�ȴ��߳���Դȫ������

};
