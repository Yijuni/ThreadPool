#include "ThreadPool.h"
/**
* pool.SubmitTask(sum1,10,20);
* pool.SubmitTask(sum2,1,2,3)
* SubmitTask:可变参模板编程
* 
* C++11线程库： thread packaged_task(function函数对象)  async 
* future代替Result
*/

int sum1(int a, int b) {
	std::this_thread::sleep_for(std::chrono::seconds(1));
	return a + b;
}
int sum2(int a, int b, int c) {
	std::this_thread::sleep_for(std::chrono::seconds(1));
	return a + b + c;
}

int main() {
	ThreadPool pool;
	//pool.SetMode(ThreadPoolMod::MODE_CACHED);
	pool.Start(2);
	std::future<int>  r1 = pool.SubmitTask(sum1, 122, 1222);
	std::future<int> r2 = pool.SubmitTask(sum2, 122, 111, 111);
	std::future<int>  r3 = pool.SubmitTask(sum1, 1242, 1222);
	std::future<int> r4 = pool.SubmitTask([](int a,int b,int c,int d)->int{
		return a + b + c + d;
		},1122, 122, 1411, 111);
	std::future<int> r5 = pool.SubmitTask(sum2, 10000000, 14000, 514);
	std::cout <<">>>>>>"<< r1.get() << ">>>>>"<<r2.get()<<std::endl;
	std::cout << ">>>>>>" << r3.get() << ">>>>>" << r4.get() << std::endl;
	std::cout << ">>>>>>" << r5.get()  << std::endl;
	return 0;
}