#ifndef COMPILER_ONLINE
#include "header.cpp"
#include "answer.cpp"
#endif

int g_pass = 0, g_total = 0;
#define RUN_TEST(name, cond) do { ++g_total; if(cond) ++g_pass; } while(0)

void test1()
{
    RUN_TEST("回文数121", Solution().isPalindrome(121));
}

void test2()
{
    RUN_TEST("回文数-121", !Solution().isPalindrome(-121));
}

void test3()
{
    RUN_TEST("回文数10", !Solution().isPalindrome(10));
}

int main()
{
    test1();
    test2();
    test3();

    std::cout << "PASSRATE " << g_pass << "/" << g_total << std::endl;
    return 0;
}
