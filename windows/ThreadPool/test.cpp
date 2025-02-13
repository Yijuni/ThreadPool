#include "ThreadPool.h"
#include<iostream>
#include <chrono>
class MyTask :public Task {
public:
	MyTask(int begin,int end):
		begin_m(begin),end_m(end)
	{
	}
	MyTask() = default;
	~MyTask() = default;
	Any Run() {
		std::cout << "tid: " << std::this_thread::get_id() << "begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(5));
		long long sum = 0;
		for (int i = begin_m; i <= end_m; i++) {
			sum += i;
		}
		std::cout << "tid: " << std::this_thread::get_id() << "end!" << std::endl;
		return sum;
	}
private:
	int begin_m;
	int end_m;
};

int main() {
	ThreadPool pool;
	pool.SetMode(ThreadPoolMod::MODE_CACHED);
	pool.Start(3);
	Result res1 = pool.SubmitTask(std::make_shared<MyTask>(1,100));
	Result res2 = pool.SubmitTask(std::make_shared<MyTask>(101,1000));
	Result res3 = pool.SubmitTask(std::make_shared<MyTask>(1001,10000));
	Result res4 = pool.SubmitTask(std::make_shared<MyTask>(10001,100000));
	Result res5 = pool.SubmitTask(std::make_shared<MyTask>(100001, 200000));
	long long sum = 0;
	long long valid = 0;
	sum += res1.Get().Cast_<long long>();
	sum += res2.Get().Cast_<long long>();
	sum += res3.Get().Cast_<long long>();
	sum += res4.Get().Cast_<long long>();
	sum += res5.Get().Cast_<long long>();
	std::cout << "最终结果" << std::endl;
	std::cout << sum << std::endl;
	for (int i = 1; i <= 200000; i++) {
		valid += i;
	}
	std::cout << "对比结果" << std::endl;
	std::cout << valid << std::endl;
	char c = std::getchar();
	return 0;
}