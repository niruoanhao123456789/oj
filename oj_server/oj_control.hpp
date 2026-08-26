#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <ctype.h>
#include <atomic>
#include <jsoncpp/json/json.h>
#include "oj_filemodel.hpp"
#include "oj_mysqlmodel.hpp"
#include "oj_view.hpp"
#include "../common/log/Log.hpp"
#include "../common/Util.hpp"

namespace oj_control
{
    using namespace LogModule;
    using namespace oj_util;
    using namespace httplib;
    using namespace oj_mysqlmodel;
    // using namespace oj_filemodel;
 
    class Machine
    {
    public:
        Machine()
        :_compile_server_ip("")
        ,_compile_server_port(0)
        ,_load(0)
        ,_plock(nullptr)
        {}

        void IncLoad()
        {         
            ++_load;
        }

        void DecLoad()
        {
            --_load;           
        }

        void ResetLoad()
        {
            _load = 0;
        }

        size_t GetLoad()
        {
            size_t load;
            if(_plock) _plock->lock();
            load = _load;
            if(_plock) _plock->unlock();

            return load;
        }

        void SetCompileServerIp(std::string& ip)
        {
            _compile_server_ip = ip;
        }

        void SetCompileServerPort(size_t port)
        {
            _compile_server_port = port;
        }

        void SetMachineLoad(size_t load)
        {
            _load = load;
        }

        void SetMutex(std::mutex* mutex)
        {
            _plock = mutex;
        }

        std::string GetCompileServerIp()
        {
            return _compile_server_ip;
        }

        size_t GetCompileServerPort()
        {
            return _compile_server_port;
        }

        size_t GetMachineLoad(size_t load)
        {
            return _load;
        }

        std::mutex* GetMutex()
        {
            return _plock;
        }
        
    private:
        std::string _compile_server_ip;
        size_t _compile_server_port;
        std::atomic<size_t> _load;                       // 编译服务的负载
        std::mutex* _plock;                 // mutex禁止拷贝的，使用指针
    };

    const std::string service_machine = "./conf/service_machine.conf";

    class LoadBlance
    {
    public:
        LoadBlance()
        {
            assert(LoadConf(service_machine));
            LOG_INFOR(GetLogger("oj_Logger"),"%s",(service_machine + "load succeed!").c_str());
        }

        bool LoadConf(const std::string& machine_conf)
        {
            std::ifstream in(machine_conf);
            if(!in.is_open())
            {
                LOG_FATAL(GetLogger("oj_Logger"),"%s",(machine_conf + "open failed!").c_str());
                return false;
            }
            std::string line;
            while(std::getline(in,line))
            {
                std::vector<std::string> tokens;
                UtilString::SplitString(line,&tokens,":");
                if(tokens.size()!=2)
                {
                    LOG_WARNNING(GetLogger("oj_Logger"),"%s",(line + "split failed").c_str());
                    continue;
                }

                Machine machine;
                machine.SetCompileServerIp(tokens[0]);
                machine.SetCompileServerPort(std::atoll(tokens[1].c_str()));
                machine.SetMachineLoad(0);
                machine.SetMutex(new std::mutex());

                _online.emplace_back(_machines.size());
                _machines.emplace_back(machine);
            }

            in.close();
            return true;
        }

        bool SmartChoice(size_t* id,Machine** m)
        {
            // 1. 使用选择好的主机(更新该主机的负载)
            // 2. 我们需要可能离线该主机
            _lock.lock();
            // 负载均衡的算法
            // 1. 随机数+hash
            // 2. 轮询+hash
            size_t n = _online.size();
            if(!n)
            {
                _lock.unlock();
                LOG_FATAL(GetLogger("oj_Logger"),"%s","All machines are offline! Please check!");
                return false;
            }
            // 找到所有负载最小的机器
            *id = _online[0];
            *m = &_machines[_online[0]];
            size_t min_load = _machines[_online[0]].GetLoad();
            for(size_t i=0;i<n;i++)
            {
                size_t cur_load = _machines[_online[i]].GetLoad();
                if(min_load > cur_load)
                {
                    min_load = cur_load;
                    *id = _online[i];
                    *m = &_machines[_online[i]];
                }
            }
            _lock.unlock();
            return true;
        }

        void OfflineMachine(size_t whichId)
        {
            _lock.lock();
            for(auto iter = _online.begin();iter != _online.end(); ++iter)
            {
                if(*iter == whichId)
                {
                    _machines[whichId].ResetLoad();

                    _online.erase(iter);
                    _offline.emplace_back(whichId);

                    break;
                    // 在break之后，iter不在被使用，所以暂时不用担心其迭代器失效的问题
                }
            }
            _lock.unlock();
        }

        void OnlineAllMachines()
        {
            _lock.lock();
            _online.insert(_online.end(),_offline.begin(),_offline.end());
            _lock.unlock();

            LOG_INFOR(GetLogger("oj_Logger"),"%s","All machines are online");
        }

        // just for test
        void ShowMachines()
        {
            _lock.lock();
            std::cout << "当前在线主机列表: ";
            for(auto &id : _online)
            {
                std::cout << id << " ";
            }
            std::cout << std::endl;
            std::cout << "当前离线主机列表: ";
            for(auto &id : _offline)
            {
                std::cout << id << " ";
            }
            std::cout << std::endl;
            _lock.unlock();
        }

    private:
        std::vector<Machine> _machines;
        std::vector<size_t> _online;
        std::vector<size_t> _offline;
        std::mutex _lock;
    };

    class Control
    {
    public:
        void RecoveryMachine()
        {
            _loadblance.OnlineAllMachines();
        }

        //根据题目数据构建网页
        // html: 输出型参数
        void AllQuestions(std::string* html)
        {
            std::vector<Question> all_questions;
            _model.GetAllQuestions(&all_questions);

            sort(all_questions.begin(),all_questions.end(),
            [](const Question& q1,const Question& q2)
            {
                return std::atoll(q1._id.c_str()) < std::atoll(q2._id.c_str());
            });

            // 获取题目信息成功，将所有的题目数据构建成网页
            _view.AllExpandHtml(all_questions,html);
        }

        void OneQuestion(const std::string& id, std::string* html)
        {
            Question q;
            _model.GetOneQuestion(id,&q);
            _view.OneExpandHtml(q,html);
        }

        void Judge(const std::string& id,const std::string& in_json,std::string* out_json)
        {
            // 0. 根据题目编号，直接拿到对应的题目细节
            Question q;
            _model.GetOneQuestion(id, &q);

            // 1. in_json进行反序列化，得到题目的id，得到用户提交源代码，input
            Json::Reader reader;
            Json::Value in_value;
            reader.parse(in_json,in_value);
            std::string code = in_value["code"].asString();

            // 2. 重新拼接用户代码+测试用例代码，形成新的代码
            Json::Value compile_value;
            compile_value["input"] = in_value["input"].asString();
            compile_value["code"] = q._header + "\n" + code + "\n" + q._tail;
            compile_value["cpu_limit"] = q._cpu_limit;
            compile_value["mem_limit"] = q._mem_limit;

            Json::FastWriter writer;
            std::string compile_string = writer.write(compile_value);

            // 3. 选择负载最低的主机(差错处理)
            // 规则: 一直选择，直到主机可用，否则，就是全部挂掉
            while(1)
            {
                size_t id = 0;
                Machine* pm = nullptr;
                if(!_loadblance.SmartChoice(&id,&pm))
                    break;

                // 4. 然后发起http请求，得到结果
                Client client(pm->GetCompileServerIp(),pm->GetCompileServerPort());
                pm->IncLoad();
                LOG_INFOR(GetLogger("oj_Logger"),"%s%zu%s%zu",
                "Choosing machine succeed! Its id: ",id,(" Ip: " + pm->GetCompileServerIp()+":").c_str(),pm->GetCompileServerPort());

                if(auto ret = client.Post("/compile_and_run",compile_string,"application/json;charset=utf-8"))
                {
                    // 5. 将结果赋值给out_json
                    if(ret->status == 200)
                    {
                        *out_json = ret->body;
                        pm->DecLoad();
                        LOG_INFOR(GetLogger("oj_Logger"),"%s","Requst compile and run succeed!");
                        break;
                    }
                    pm->DecLoad();
                }
                else
                {
                    LOG_ERROR(GetLogger("oj_Logger"),"%s%zu%s%zu",
                    "Machine maybe offline! Its id: ",id,(" Ip: " + pm->GetCompileServerIp()+":").c_str(),pm->GetCompileServerPort());
                    _loadblance.OfflineMachine(id);
                    _loadblance.ShowMachines();
                }
            }
        }

    private:
        Model _model;
        oj_view::View _view;
        LoadBlance _loadblance;
    };
}