#pragma once
#include <iostream>
#include <string>
#include <unistd.h>
#include "../log/Log.hpp"
#include "../common/Util.hpp"

namespace oj_complier
{
    using namespace LogModule;

    class Complier
    {
    public:
        
        static bool Complie(const std::string& filename)
        {
            pid_t pid = fork();
            if(pid == 0)
            {
                //程序替换，并不影响进程的文件描述符表
                //子进程: 调用编译器，完成对代码的编译工作
                //g++ -o target src -std=c++11
                execlp("g++","-o",oj_util::Path::Exe(filename),oj_util::Path::Src,"-std==c++20");
            }
        }
    };
}