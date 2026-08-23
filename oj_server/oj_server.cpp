#include <iostream>
#include <httplib.h>

using namespace httplib;

int main()
{
    // 处理用户路由服务功能
    Server svr;

    svr.listen("0.0.0.0",8080);

    return 0;
}