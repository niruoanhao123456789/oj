#ifndef COMPILER_ONLINE
#include "header.cpp"
#endif

void test1()
{
    // 通过定义临时对象，来完成方法的调用
    bool ret = Solution().isPalindrome(121);
    if(ret)
        std::cout << "用例1 Acesss, 测试121通过 ... OK!" << std::endl;
    else
        std::cout << "用例1 failed, 测试的值是: 121"  << std::endl;

}

void test2()
{
    // 通过定义临时对象，来完成方法的调用
    bool ret = Solution().isPalindrome(-121);
    if(!ret)
        std::cout << "用例2 Acesss, 测试-121通过 ... OK!" << std::endl;
    else
        std::cout << "用例2 failed, 测试的值是: -121"  << std::endl;
}

void test3()
{
    // 通过定义临时对象，来完成方法的调用
    bool ret = Solution().isPalindrome(10);
    if(ret)
        std::cout << "用例3 Acesss, 测试10通过 ... OK!" << std::endl;
    else
        std::cout << "用例3 failed, 测试的值是: 10"  << std::endl;
}

int main()
{
    test1();
    test2();
    test3();

    return 0;
}