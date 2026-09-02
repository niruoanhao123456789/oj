#ifndef COMPILER_ONLINE
#include "header.cpp"
#include "answer.cpp"
#endif

int g_pass = 0, g_total = 0;
#define RUN_TEST(name, cond) do { ++g_total; if(cond) ++g_pass; } while(0)

void test()
{
    std::vector<int> v = {1,2,3,4,5,6,12,3,4,-1};
    RUN_TEST("最大值12", Solution().Max(v) == 12);
}

int main()
{
    test();

    std::cout << "PASSRATE " << g_pass << "/" << g_total << std::endl;
    return 0;
}
