#pragma once
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include "../common/Util.hpp"
#include "../common/log/Log.hpp"

namespace oj_runner
{
    using namespace LogModule;
    using namespace oj_util;

    class Runner
    {
    public:
        // 
        static void SetResoureLimit(size_t cpu_limit, size_t mem_limit)
        {
            struct rlimit cpu_rlimit;
            cpu_rlimit.rlim_max = RLIM_INFINITY;
            cpu_rlimit.rlim_cur = cpu_limit;
            setrlimit(RLIMIT_CPU, &cpu_rlimit);

            struct rlimit mem_rlimit;
            mem_rlimit.rlim_max = RLIM_INFINITY;
            mem_rlimit.rlim_cur = mem_limit * 1024 * 1024; //转化成为MB
            setrlimit(RLIMIT_AS, &mem_rlimit);
        }


        /* 返回类型：int
        *  > 0  存在异常退出
        *  == 0 正常退出
        *  < 0  内部异常
        */
        static int Run(const std::string& filename, size_t cpu_limit, size_t mem_limit)
        {
            // Run 只需关心程序是否运行完，结果是否正确交予上层
            const std::string _execute = Path::Exe(filename);
            const std::string _stdin = Path::Stdin(filename);
            const std::string _stdout = Path::Stdout(filename);
            const std::string _stderr = Path::Stderr(filename);

            umask(0);
            int _stdin_fd = open(_stdin.c_str(), O_CREAT | O_RDONLY, 0644);
            int _stdout_fd = open(_stdout.c_str(), O_CREAT | O_WRONLY, 0644);
            int _stderr_fd = open(_stderr.c_str(), O_CREAT | O_WRONLY, 0644);

            if(_stdin_fd < 0 || _stdout_fd < 0 || _stderr_fd < 0)
            {
                LOG_ERROR(GetLogger("Async_Loggger"),"%s","stdin, stdout or stderr open failed!");
                return -1;
            }

            pid_t pid = fork();
            if(pid == 0)
            {
                dup2(_stdin_fd, 0);
                dup2(_stdout_fd, 1);
                dup2(_stderr_fd, 2);

                SetResoureLimit(cpu_limit,mem_limit);
                execl(_execute.c_str(),_execute.c_str(),nullptr);

                LOG_ERROR(GetLogger("Async_Loggger"),"%s","execl failed!");
                exit(1);
            }
            else if(pid < 0)
            {
                close(_stdin_fd);
                close(_stdout_fd);
                close(_stderr_fd);
                LOG_ERROR(GetLogger("Async_Loggger"),"%s","fork failed!");
                return -2;
            }

            close(_stdin_fd);
            close(_stdout_fd);
            close(_stderr_fd);
            int status = 0;
            waitpid(pid,&status,0);

            // 如果程序运行异常，一定是因为因为收到了信号！
            LOG_INFOR(GetLogger("Async_Loggger"),"%s%d","running finished! Singal: ",status & 0x7F);
            return status & 0x7F;
        }
    };
}