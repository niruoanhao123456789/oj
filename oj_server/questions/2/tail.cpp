#ifndef COMPILER_ONLINE
#include "header.cpp"
#endif

void test1()
{
    int ans = 12;
    std::vector<int> v = {1,2,3,4,5,6,12,3,4,-1};
    int ret = Solution().Max(v);
    
    if(ret==ans)
        std::cout << "用例1 Acesss, 测试{1,2,3,4,5,6,12,3,4,-1}通过 ... OK!" << std::endl;
    else
        std::cout << "用例1 failed, 输出为: "<<ret<<std::endl;
}

int main()
{
    test1();

    return 0;
}