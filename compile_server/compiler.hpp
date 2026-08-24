#pragma once
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../common/log/Log.hpp"
#include "../common/Util.hpp"

namespace oj_compiler
{
    using namespace LogModule;

    class Compiler
    {
    public:
        
        static bool Compile(const std::string& filename)
        {
            pid_t pid = fork();
            if(pid == 0)
            {
                umask(0);
                int errfd = open(oj_util::Path::CompilerError(filename).c_str(), O_CREAT | O_WRONLY, 0644);
                if(errfd<0)
                {
                    LOG_WARNNING(GetLogger("CompileRun_Loggger"),"%s","open errfd failed!");
                    exit(1);
                }
                //重定向标准错误到errfd
                dup2(errfd, 2);

                //程序替换，并不影响进程的文件描述符表
                //子进程: 调用编译器，完成对代码的编译工作
                //g++ -o target src -std=c++11
                execlp("g++","g++","-o",oj_util::Path::Exe(filename).c_str(),oj_util::Path::Src(filename).c_str(),"-std=c++20",nullptr);

                LOG_ERROR(GetLogger("CompileRun_Loggger"),"%s","g++ failed, maybe args wrong.");
            }
            else if(pid < 0)
            {
                LOG_ERROR(GetLogger("CompileRun_Loggger"),"%s","fork error!");
            }

            waitpid(pid,nullptr,0);
            if(oj_util::File::IsFileExists(oj_util::Path::Exe(filename)))
            {
                LOG_INFOR(GetLogger("CompileRun_Loggger"),"%s",(oj_util::Path::Src(filename)+" compilation succeed!").c_str());
                return true;
            }
            LOG_ERROR(GetLogger("CompileRun_Loggger"),"%s","compilation failed!");
            return false;
        }
    };
}