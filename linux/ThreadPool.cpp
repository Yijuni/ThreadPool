#include "ThreadPool.h"
#include <iostream>
const int TASK_MAX_THRESHOLD = INT32_MAX;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_IDLE_MAX_TIME = 60;//�߳�������ʱ��10s

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
	//��¼��ʼ�߳�����
	initThreadSize_m = initThreadSize;
	poolIsRunning_m = true;
	
	//�����̶߳���
	for (int i = 0; i < initThreadSize_m; i++) {
		//����thread�̶߳����ʱ�򣬰��̳߳صĺ�������thread�̶߳���,�����̶߳���ִ�еĺ����Ϳ��Է����̳߳��ڵ���Դ������
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
		int threadId = ptr->GetId();
		threadPool_m.emplace(threadId,std::move(ptr));//emplace_back�����unique_ptr�Ŀ������죬����unique_ptr�������������죬ֻ֧���ƶ�����
	}
	//���������߳�
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
	
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueueMtx_m);
	//�߳�ͨ�� �ȴ���������пռ�
	//while (taskQueue_m.size() == taskQueueMaxThreshold_m) {
	//	notFullCond_m.wait(lock);
	//}
	//�Ȼ�ȡ�������ж������������㣬������ֱ�����������ͷ���
	//�û��ύ���������ʱ�䲻�ܳ�1s�������ж������ύʧ��
	bool  flag = notFullCond_m.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQueue_m.size() < taskQueueMaxThreshold_m; });
	if (!flag) {
		//�ȴ���1s��������Ȼ�����ģ��Ǿ������ύʧ��
		std::cerr << "task queue is full ,submit task fail." << std::endl;
		return Result(task,false);
	}
	//����Ž����������
	taskQueue_m.emplace(task);
	taskSize_m++;
	//֪ͨ�������߳��������ȥִ��
	notEmptyCond_m.notify_all();

	//cached �������ȽϽ�����С��������� �������������Ϳ����߳��������ж��Ƿ���Ҫ��չ�߳�,���߳������ܳ�����ֵ
	if (poolMod_m == ThreadPoolMod::MODE_CACHED && taskSize_m>idleThreadSize_m && curThreadSize_m< threadMaxThreshold_m) {
		//�������߳�
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
	auto lastRunTime = std::chrono::high_resolution_clock().now();//�߾���ʱ��
	std::cout << "begin threadFunc t_id="<< std::this_thread::get_id() << std::endl;
	//��������ִ������˳��߳�
	while(true) {
		std::shared_ptr<Task> task;
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
				//�̳߳�Ҫ�˳��������߳���Դ��
				//if (!poolIsRunning_m) {
				//	//�����߳�
				//	threadPool_m.erase(threadId);
				//	curThreadSize_m--;
				//	idleThreadSize_m--;
				//	std::cout << "thread id : " << std::this_thread::get_id() << "exit!" << std::endl;
				//	exitCond_m.notify_all();
				//	return;
				//}
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
			task->exec();//ִ���û��ύ���񲢴���Result
			//����ִ���֪꣬ͨ
		}
		idleThreadSize_m++;//������������߳�������
		lastRunTime = std::chrono::high_resolution_clock().now();//��������ִ�����ʱ��
	}
	//������whileѭ��˵���̳߳�Ҫ������

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
	//����һ���߳���ִ��һ���̺߳���
	std::thread t(func_m,threadId_m);
	t.detach();//�����̣߳���ֹ���񱻹ҵ�

}


int Thread::GetId() const
{
	return threadId_m;
}


Semaphore::Semaphore(int source) :source_m(source),isExit_m(false)
{
}

Semaphore::~Semaphore()
{
	isExit_m = true;
}

void Semaphore::wait()
{
	if(isExit_m){
		return;
	}
	std::unique_lock<std::mutex> lock(mutex_m);
	cond_m.wait(lock, [&]()->bool {return source_m > 0; });
	source_m--;
}

void Semaphore::post()
{
	if(isExit_m){
		return;
	}
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
	any_m = std::move(res);//����ִ����ѽ����Result�ڵ�any_m
	sem_m.post();//����ִ���꣬���������������ź���+1(source=1)
}

Any Result::Get()
{
	if (!isValid_m) {
		return "";
	}
	sem_m.wait();//����ûִ���꣬���λ�û�����(source_m=0)
	return std::move(any_m);
}

Task::Task():res_m(nullptr)
{
}

void Task::exec()
{
	if (res_m == nullptr) return;
	res_m->SetVal(Run());//���﷢����̬���ã������Ϳ��԰��û�ִ�����񣬶��һ��ܽ�����������������Resultִ������)
}

void Task::setResult(Result* res)
{
	res_m = res;
}
