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
#include <unordered_map>

//Any���Ϳɽ�����������
class Any {
public:
	Any() = default;
	~Any() = default;
	//��Ϊ������unique_ptr��֧�ֿ�������ֻ֧���ƶ����죬����Ҫ��ֵ����
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data):base_m(std::make_unique<Derive<T>>(data))
	{}

	//��������ܰ�Any��������洢��������ȡ��
	template<typename T>
	T Cast_() {
		//��base_m��ȡ����������ڵ�data_m��Ա����
		//����ָ��=��������ָ��           RTTI����ʶ�𣬻����Ƿ����ת��ָ��,�������ת���ͷ���nullptr
		//���� base_mָ���ʵ������Derive<int>���󣬵����㴫���T��long,Derive<int> -> Derive<long>�ǻ�ת��ʧ��
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_m.get());
		if (pd == nullptr) {
			throw "type is incompatiable!";//���쳣
		}
		return pd->data_m;
	}
private:
	//��������
	class Base {
	public:
		virtual ~Base() = default;
	};

	//����������
	template<typename T>
	class Derive : public Base {
	public:
		Derive(T data):data_m(data){}
		~Derive() {};
		T data_m;
	};

	std::unique_ptr<Base> base_m;
};

//�ź�������
class Semaphore {
public:
	Semaphore(int source=0);
	~Semaphore();
	//��ȡһ���ź�����Դ
	void wait();
	//����һ���ź�����Դ
	void post();
private:
	int source_m;
	std::atomic_bool isExit_m;///linux上，防止死锁的变量
	std::mutex mutex_m;
	std::condition_variable cond_m;
};


class Task;
//������ɺ�ķ���ֵ����
class Result {
public:
	Result();
	Result(std::shared_ptr<Task> task,bool isValid=true);
	~Result();
	void SetVal(Any res);//��task����
	Any Get();//���û�����
private:
	Semaphore sem_m;//�ź���
	Any any_m;//���񷵻�ֵ
	std::shared_ptr<Task> task_m;
	std::atomic_bool isValid_m;//����ֵ�Ƿ���Ч����������ύʧ�ܣ�����ֵ������Ч��
};

//������������
class Task {
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	//�û����Զ����������ͣ���Task�̳У���д����
	virtual Any Run() = 0;
private:
	//std::shared_ptr<Result> res_m; ���ﲻ����ǿ����ָ�룬�����������⣬�ᵼ������ָ���޷��ͷ�
	Result* res_m; //Result�������ڳ���Task
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
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();
	//�߳�����
	void Start();
	//��ȡ�߳�ID
	int GetId() const;
private:
	ThreadFunc func_m;
	static int generateId_m;
	int threadId_m;//�����߳�ID
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
	//�����̳߳�,Ĭ��ֵϵͳCPU������
	void Start(int initThreadSize=std::thread::hardware_concurrency());
	//���������������������ֵ
	void SetTaskQueueMaxThreshold(int threshold);
	//���̳߳��ύ����
	Result SubmitTask(std::shared_ptr<Task> task);
	//�����̳߳�cachedģ�����߳�����ֵ
	void SetThreadMaxThreshold(int threshold);
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//�����̺߳���,�����ڲ�ͬ���߳��У��������������ȡ����ִ��
	void ThreadFunc(int threadId);//�����߳�id���������ɾ���߳�
	//���pool������״̬
	bool CheckRunningState() const;

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

	//������ָ�룬��ֹ����δִ���꣬����ͱ��ͷ��ˣ����������ӳ���
	std::queue<std::shared_ptr<Task>> taskQueue_m; //�������
	std::atomic_uint taskSize_m; //��������(ԭ�����ͣ������͵������޸Ķ���ԭ�Ӳ�����
	size_t taskQueueMaxThreshold_m; //��������������ֵ
	std::mutex taskQueueMtx_m; //������е��̰߳�ȫ
	std::condition_variable notFullCond_m; //������в���
	std::condition_variable notEmptyCond_m; //������в���	
	std::condition_variable exitCond_m;//�ȴ��߳���Դȫ������

};

#endif // ! THREADPOOL_H



