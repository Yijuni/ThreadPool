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
	//��¼��ʼ�߳�����
	initThreadSize_m = initThreadSize;

	//�����̶߳���
	for (int i = 0; i < initThreadSize_m; i++) {
		//����thread�̶߳����ʱ�򣬰��̳߳صĺ�������thread�̶߳���,�����̶߳���ִ�еĺ����Ϳ��Է����̳߳��ڵ���Դ������
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this));
		threadPool_m.emplace_back(std::move(ptr));//emplace_back�����unique_ptr�Ŀ������죬����unique_ptr�����������죬ֻ֧���ƶ�����
	}
	//���������߳�
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
		return;
	}
	//����Ž����������
	taskQueue_m.emplace(task);
	taskSize_m++;
	//֪ͨ�������߳��������ȥִ��
	notEmptyCond_m.notify_all();
}


void ThreadPool::ThreadFunc()
{
	std::cout << "begin threadFunc t_id="<< std::this_thread::get_id() << std::endl;
	while(true) {
		std::shared_ptr<Task> task;
		{//��ȡ������֮��ȷ�����Զ��ͷţ��������߳�ʹ�ã����߳�ִ��ȡ��������(����ͬһʱ��ֻ��һ���߳���ִ������
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueueMtx_m);
			std::cout << "tid: " << std::this_thread::get_id() << "���Ի�ȡ����!" << std::endl;
			//�ȴ�notEmpty��������
			notEmptyCond_m.wait(lock, [&]()->bool {return taskSize_m > 0; });
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
		}

		if (task != nullptr) {
			std::cout << "tid: " << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;
			task->Run();//��ִ̬���û��Զ��������
			//����ִ���֪꣬ͨ
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
	//����һ���߳���ִ��һ���̺߳���
	std::thread t(func_m);
	t.detach();//�����̣߳���ֹ���񱻹ҵ�

}
