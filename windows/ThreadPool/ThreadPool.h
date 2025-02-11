#ifndef  THREADPOOL_H //���δ����THREADPOOL_H�������������Ч
#define THREADPOOL_H //����һ��THREADPOOL_H����ֹ��ͻ����ֹ��ΰ�����δ���)

#include<vector>
#include<condition_variable>
#include<mutex>
#include<queue>
#include<atomic>
#include<memory>
#include <functional>
#include <thread>
//������������
class Task {
public:
	//�û����Զ����������ͣ���Task�̳У���д����
	virtual void Run() = 0;
};

//�̳߳�����ģʽ
enum class ThreadPoolMod{//��class��ֹö����������ͻ
	MODE_FIXED,//�̶������̣߳������ں���ȷ��
	MODE_CACHED,//�߳������ɶ�̬����
};

//�߳�����
class Thread
{
public:
	using ThreadFunc = std::function<void()>;
	Thread();
	Thread(ThreadFunc func);
	~Thread();
	//�߳�����
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
//�̳߳�
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	//�����̳߳�ģʽ
	void SetMode(ThreadPoolMod mode=ThreadPoolMod::MODE_FIXED);
	//�����̳߳�
	void Start(int initThreadSize=4);
	//���������������������ֵ
	void SetTaskQueueMaxThreshold(int threshold);
	//���̳߳��ύ����
	void SubmitTask(std::shared_ptr<Task> task);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//�����̺߳���,�����ڲ�ͬ���߳��У��������������ȡ����ִ��
	void ThreadFunc();

	std::vector<std::unique_ptr<Thread>> threadPool_m;//�̳߳�
	size_t initThreadSize_m;//��ʼ�߳���
	ThreadPoolMod poolMod_m;
	//������ָ�룬��ֹ����δִ���꣬����ͱ��ͷ��ˣ����������ӳ���
	std::queue<std::shared_ptr<Task>> taskQueue_m; //�������
	std::atomic_uint taskSize_m; //��������(ԭ�����ͣ������͵������޸Ķ���ԭ�Ӳ�����
	size_t taskQueueMaxThreshold_m; //��������������ֵ

	std::mutex taskQueueMtx_m; //������е��̰߳�ȫ
	std::condition_variable notFullCond_m; //������в���
	std::condition_variable notEmptyCond_m; //������в���
	

};

#endif // ! THREADPOOL_H



