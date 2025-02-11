#include "ThreadPool.h"
#include<iostream>
#include <chrono>
class MyTask :public Task {
public:
	MyTask(int begin,int end):
		begin_m(begin),end_m(end)
	{
	}
	MyTask();
	~MyTask();
	void Run() {
		std::cout << "tid: " << std::this_thread::get_id() << "begin!" << std::endl;
		int sum = 0;
		for (int i = begin_m; i <= end_m; i++) {
			sum += i;
		}
		std::cout << "tid: " << std::this_thread::get_id() << "end!" << std::endl;
	}
private:
	int begin_m;
	int end_m;
};

int main() {
	ThreadPool pool;
	pool.Start(5);
	for (int i = 0; i < 12; i++) {
		pool.SubmitTask(std::make_shared<MyTask>());
	}
	char c = std::getchar();
	return 0;
}