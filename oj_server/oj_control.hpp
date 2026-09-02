#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <ctype.h>
#include <atomic>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>
#include <jsoncpp/json/json.h>
#include "oj_filemodel.hpp"
#include "oj_mysqlmodel.hpp"
#include "oj_view.hpp"
#include "user/oj_user_model.hpp"
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

        // std::atomic 使 Machine 不可拷贝, 提供移动构造以支持 vector 扩容
        Machine(Machine&& other) noexcept
        :_compile_server_ip(std::move(other._compile_server_ip))
        ,_compile_server_port(other._compile_server_port)
        ,_load(other._load.load())
        ,_plock(other._plock)
        {
            other._plock = nullptr;
        }

        Machine& operator=(Machine&& other) noexcept
        {
            if(this != &other)
            {
                _compile_server_ip = std::move(other._compile_server_ip);
                _compile_server_port = other._compile_server_port;
                _load = other._load.load();
                _plock = other._plock;
                other._plock = nullptr;
            }
            return *this;
        }

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

                _machines.emplace_back();
                Machine& machine = _machines.back();
                machine.SetCompileServerIp(tokens[0]);
                machine.SetCompileServerPort(std::atoll(tokens[1].c_str()));
                machine.SetMachineLoad(0);
                machine.SetMutex(new std::mutex());

                _online.emplace_back(_machines.size() - 1);
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

        // 根据题目编号判断当前用户是否可见:
        // 全局题所有人可见; 管理员可见全部; 负责人仅见本组题; 普通用户仅见所在组题
        bool CanAccessQuestion(const oj_user_model::User* user,const Question& q)
        {
            if(q._scope == "global")
                return true;
            if(user == nullptr)
                return false;
            if(user->_role == "admin")
                return true;
            int gid = std::atoi(q._scope.c_str());
            if(gid <= 0)
                return false;
            if(user->_role == "leader")
                return LeaderOwnsGroup(user->_id,gid);
            return _user_model.IsMember(user->_id,gid);
        }

        //根据题目数据构建网页
        // html: 输出型参数
        // 未登录不能访问(页面仅对登录用户开放); 已登录后按角色与小组过滤可见性
        void AllQuestions(const std::string& auth,std::string* html)
        {
            oj_user_model::User cur;
            if(!GetSessionUser(auth,&cur))
            {
                *html = MessagePage("请先登录","登录后才能浏览题目列表。");
                return;
            }

            std::vector<Question> all_questions;
            _model.GetAllQuestions(&all_questions);

            // 按当前用户的角色与所属小组过滤可见性
            std::vector<Question> visible;
            for(const auto& q : all_questions)
                if(CanAccessQuestion(&cur,q))
                    visible.push_back(q);

            sort(visible.begin(),visible.end(),
            [](const Question& q1,const Question& q2)
            {
                return std::atoll(q1._id.c_str()) < std::atoll(q2._id.c_str());
            });

            // 获取题目信息成功，将所有的题目数据构建成网页
            _view.AllExpandHtml(visible,true,html);
        }

        void OneQuestion(const std::string& auth,const std::string& id,std::string* html)
        {
            oj_user_model::User cur;
            if(!GetSessionUser(auth,&cur))
            {
                *html = MessagePage("请先登录","登录后才能查看与解答题目。");
                return;
            }

            Question q;
            _model.GetOneQuestion(id,&q);

            if(q._id.empty())
            {
                *html = MessagePage("题目不存在","该题目不存在或已被删除。");
                return;
            }
            if(!CanAccessQuestion(&cur,q))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s%s%s","visibility rejected! question: ",id.c_str(),(" user: " + cur._username).c_str());
                *html = MessagePage("无法访问","这道题目仅对可见范围开放，你可能还未加入对应的小组。");
                return;
            }
            _view.OneExpandHtml(q,true,html);
        }

        void Judge(const std::string& auth,const std::string& id,const std::string& in_json,std::string* out_json)
        {
            // 0. 未登录不允许提交评测
            oj_user_model::User cur;
            if(!GetSessionUser(auth,&cur))
            {
                Json::Value out;
                out["status"] = -2;
                out["reason"] = "请先登录后再提交评测";
                *out_json = WriteJson(out);
                return;
            }

            // 1. 根据题目编号，直接拿到对应的题目细节
            Question q;
            _model.GetOneQuestion(id, &q);

            // 1.5 可见性校验: 仅允许对当前用户可见的题目提交评测
            if(q._id.empty() || !CanAccessQuestion(&cur,q))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s%s%s","judge visibility rejected! question: ",id.c_str(),(" user: " + cur._username).c_str());
                Json::Value out;
                out["status"] = -2;
                out["reason"] = "题目不可访问或不存在";
                *out_json = WriteJson(out);
                return;
            }

            // 2. in_json进行反序列化，得到题目的id，得到用户提交源代码，input
            Json::Reader reader;
            Json::Value in_value;
            reader.parse(in_json,in_value);
            std::string code = in_value["code"].asString();

            // 2. 重新拼接用户代码+隐藏测试用例(tail, 判题唯一依据), 形成新的代码
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

        // ---------- 用户 / 角色 / 小组 (见 SPEC.md §5.5 ~ §5.11) ----------

        // GET /register —— 注册页(可选普通用户/负责人, 负责人需管理员邀请码)
        void RegisterPage(std::string* html)
        {
            _view.RegisterExpandHtml(html);
        }

        // GET /login —— 登录页
        void LoginPage(std::string* html)
        {
            _view.LoginExpandHtml(html);
        }

        // GET /group_manage —— 小组管理页
        // 管理员: 额外提供负责人注册邀请码管理; 负责人/管理员: 创建并管理多个小组与邀请码;
        // 普通用户: 凭小组邀请码加入小组并查看已加入的小组
        void GroupManage(const std::string& auth,std::string* html)
        {
            oj_user_model::User cur;
            if(!GetSessionUser(auth,&cur))
            {
                *html = MessagePage("无法访问","请先登录后再管理小组。");
                return;
            }

            std::vector<oj_view::GroupEntry> my_groups;
            std::vector<oj_user_model::Group> owned;
            _user_model.GetGroupsByOwner(cur._id,&owned);
            for(const auto& g : owned)
                my_groups.push_back({std::to_string(g._id),g._name,g._invite_code,g._created_at});

            std::vector<oj_view::GroupEntry> joined_groups;
            if(cur._role == "user")
            {
                std::vector<int> gids;
                _user_model.GetUserGroups(cur._id,&gids);
                for(int gid : gids)
                {
                    oj_user_model::Group g;
                    if(_user_model.GetGroupById(gid,&g))
                        joined_groups.push_back({std::to_string(g._id),g._name,g._invite_code,g._created_at});
                }
            }

            _view.GroupManageExpandHtml(cur._role,cur._username,my_groups,joined_groups,html);
        }

        static std::string WriteJson(const Json::Value& value)
        {
            Json::FastWriter writer;
            return writer.write(value);
        }

        // 校验 Authorization: Bearer <token>, 解析当前登录用户
        bool GetSessionUser(const std::string& auth,oj_user_model::User* user)
        {
            if(auth.compare(0,7,"Bearer ") != 0)
                return false;
            return _user_model.GetSession(auth.substr(7),user);
        }

        // 需要登录, 失败时向 out 写入错误信息并返回 false
        bool RequireUser(const std::string& auth,oj_user_model::User* user,Json::Value* out)
        {
            if(!GetSessionUser(auth,user))
            {
                (*out)["ok"] = false;
                (*out)["message"] = "未登录或登录已过期";
                return false;
            }
            return true;
        }

        // POST /api/register —— 注册用户: 可选普通用户或负责人(负责人需管理员邀请码)
        void Register(const std::string& in_json,std::string* out_json)
        {
            Json::Value out;
            Json::Reader reader;
            Json::Value in;
            reader.parse(in_json,in);
            std::string username = in["username"].asString();
            std::string password = in["password"].asString();
            std::string role = in["role"].asString();
            std::string invite_code = in["invite_code"].asString();
            if(role != "user" && role != "leader")
                role = "user";
            if(username.empty() || password.empty())
            {
                out["ok"] = false;
                out["message"] = "用户名和密码不能为空";
                *out_json = WriteJson(out);
                return;
            }
            if(role == "leader")
            {
                if(invite_code.empty())
                {
                    out["ok"] = false;
                    out["message"] = "注册负责人需填写管理员邀请码";
                    *out_json = WriteJson(out);
                    return;
                }
                if(!_user_model.IsValidAdminInviteCode(invite_code))
                {
                    out["ok"] = false;
                    out["message"] = "管理员邀请码无效";
                    *out_json = WriteJson(out);
                    return;
                }
            }
            std::string actual_role;
            if(!_user_model.Register(username,password,role,&actual_role))
            {
                out["ok"] = false;
                out["message"] = "注册失败: 用户名可能已存在";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["message"] = "注册成功";
            out["role"] = actual_role;
            *out_json = WriteJson(out);
        }

        // POST /api/login —— 登录校验并签发 token
        void Login(const std::string& in_json,std::string* out_json)
        {
            Json::Value out;
            Json::Reader reader;
            Json::Value in;
            reader.parse(in_json,in);
            std::string username = in["username"].asString();
            std::string password = in["password"].asString();
            oj_user_model::User user;
            if(!_user_model.Login(username,password,&user))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s%s","login failed! username: ",username.c_str());
                out["ok"] = false;
                out["message"] = "用户名或密码错误";
                *out_json = WriteJson(out);
                return;
            }
            std::string token = _user_model.CreateSession(user);
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s%s","login succeed! username: ",user._username.c_str(),(" role: " + user._role).c_str());
            out["ok"] = true;
            out["token"] = token;
            out["username"] = user._username;
            out["role"] = user._role;
            *out_json = WriteJson(out);
        }

        // PUT /api/users/{id}/role —— 管理员修改其他用户角色
        void SetUserRole(const std::string& auth,const std::string& target_id,const std::string& in_json,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "admin")
            {
                out["ok"] = false;
                out["message"] = "需要管理员权限";
                *out_json = WriteJson(out);
                return;
            }
            Json::Reader reader;
            Json::Value in;
            reader.parse(in_json,in);
            std::string role = in["role"].asString();
            if(role != "admin" && role != "leader" && role != "user")
            {
                out["ok"] = false;
                out["message"] = "非法的角色类型";
                *out_json = WriteJson(out);
                return;
            }
            int id = std::atoi(target_id.c_str());
            if(id == cur._id)
            {
                out["ok"] = false;
                out["message"] = "不能修改自己的角色";
                *out_json = WriteJson(out);
                return;
            }
            if(!_user_model.UpdateRole(id,role))
            {
                out["ok"] = false;
                out["message"] = "修改失败: 用户不存在";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["message"] = "角色已修改";
            *out_json = WriteJson(out);
        }

        // POST /api/groups —— 负责人或管理员创建小组(可创建多个)
        void CreateGroup(const std::string& auth,const std::string& in_json,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "leader" && cur._role != "admin")
            {
                out["ok"] = false;
                out["message"] = "需要负责人或管理员身份";
                *out_json = WriteJson(out);
                return;
            }
            Json::Reader reader;
            Json::Value in;
            reader.parse(in_json,in);
            std::string name = in["name"].asString();
            if(name.empty())
            {
                out["ok"] = false;
                out["message"] = "小组名称不能为空";
                *out_json = WriteJson(out);
                return;
            }
            oj_user_model::Group g;
            if(!_user_model.CreateGroup(name,cur._id,&g))
            {
                out["ok"] = false;
                out["message"] = "创建小组失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["group_id"] = g._id;
            out["invite_code"] = g._invite_code;
            *out_json = WriteJson(out);
        }

        // POST /api/admin/invite —— 管理员重置负责人注册邀请码(旧码失效)
        void ResetAdminInvite(const std::string& auth,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "admin")
            {
                out["ok"] = false;
                out["message"] = "需要管理员权限";
                *out_json = WriteJson(out);
                return;
            }
            std::string code;
            if(!_user_model.ResetAdminInvite(&code))
            {
                out["ok"] = false;
                out["message"] = "重置失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["invite_code"] = code;
            *out_json = WriteJson(out);
        }

        // POST /api/groups/join —— 普通用户凭邀请码加入小组
        void JoinGroup(const std::string& auth,const std::string& in_json,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "user")
            {
                out["ok"] = false;
                out["message"] = "需要普通用户身份";
                *out_json = WriteJson(out);
                return;
            }
            Json::Reader reader;
            Json::Value in;
            reader.parse(in_json,in);
            std::string code = in["invite_code"].asString();
            if(code.empty())
            {
                out["ok"] = false;
                out["message"] = "邀请码不能为空";
                *out_json = WriteJson(out);
                return;
            }
            oj_user_model::Group g;
            if(!_user_model.GetGroupByInvite(code,&g))
            {
                out["ok"] = false;
                out["message"] = "邀请码无效";
                *out_json = WriteJson(out);
                return;
            }
            if(_user_model.IsMember(cur._id,g._id))
            {
                out["ok"] = false;
                out["message"] = "你已在该小组中";
                *out_json = WriteJson(out);
                return;
            }
            if(!_user_model.JoinGroup(cur._id,g._id))
            {
                out["ok"] = false;
                out["message"] = "加入小组失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["group_id"] = g._id;
            out["name"] = g._name;
            out["created_at"] = g._created_at;
            *out_json = WriteJson(out);
        }

        // POST /api/groups/{id}/invite —— 负责人重置邀请码(旧码失效)
        void ResetInviteCode(const std::string& auth,const std::string& group_id,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            int gid = std::atoi(group_id.c_str());
            oj_user_model::Group g;
            if(!_user_model.GetGroupById(gid,&g))
            {
                out["ok"] = false;
                out["message"] = "小组不存在";
                *out_json = WriteJson(out);
                return;
            }
            if(g._owner_id != cur._id)
            {
                out["ok"] = false;
                out["message"] = "仅小组负责人可重置邀请码";
                *out_json = WriteJson(out);
                return;
            }
            std::string code;
            if(!_user_model.ResetInviteCode(gid,&code))
            {
                out["ok"] = false;
                out["message"] = "重置失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["invite_code"] = code;
            *out_json = WriteJson(out);
        }

        // DELETE /api/groups/{id} —— 删除小组(管理员可删任意, 负责人仅限本组)
        void DeleteGroup(const std::string& auth,const std::string& group_id,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "leader" && cur._role != "admin")
            {
                out["ok"] = false;
                out["message"] = "需要负责人或管理员身份";
                *out_json = WriteJson(out);
                return;
            }
            int gid = std::atoi(group_id.c_str());
            oj_user_model::Group g;
            if(!_user_model.GetGroupById(gid,&g))
            {
                out["ok"] = false;
                out["message"] = "小组不存在";
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "admin" && g._owner_id != cur._id)
            {
                out["ok"] = false;
                out["message"] = "仅小组负责人或管理员可删除小组";
                *out_json = WriteJson(out);
                return;
            }
            if(!_user_model.DeleteGroup(gid))
            {
                out["ok"] = false;
                out["message"] = "删除小组失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["message"] = "小组已删除";
            *out_json = WriteJson(out);
        }

        // ---------- 题目管理 (见 SPEC.md §5.13 ~ §5.16) ----------

        static bool ParseQuestionJson(const Json::Value& in,Question* q,std::string* err)
        {
            q->_title = in["title"].asString();
            q->_rank = Question::StringToRank(in["rank"].asString());
            q->_desc = in["desc"].asString();
            q->_header = in["header"].asString();
            q->_answer = in["answer"].asString();
            q->_tail = in["tail"].asString();
            q->_cpu_limit = in["cpu_limit"].asUInt64();
            q->_mem_limit = in["mem_limit"].asUInt64();
            q->_scope = in["scope"].asString();
            if(q->_scope.empty())
                q->_scope = "global";
            if(q->_title.empty())
            {
                *err = "题目标题不能为空";
                return false;
            }
            if(q->_rank == Question::UNKONW)
            {
                *err = "非法的难度";
                return false;
            }
            if(q->_tail.empty())
            {
                *err = "必须设置不可见测试案例(tail)";
                return false;
            }
            return true;
        }

        // 判断 user_id 是否拥有 group_id 小组
        bool LeaderOwnsGroup(int user_id,int group_id)
        {
            oj_user_model::Group g;
            return group_id > 0 && _user_model.GetGroupById(group_id,&g) && g._owner_id == user_id;
        }

        std::string MessagePage(const std::string& title,const std::string& message)
        {
            // 若浏览器本地保存了 token, 则自动携带 Authorization 重新请求当前页面,
            // 让直接访问受限页面(如小组题/管理页)的用户无需手动跳转即可恢复可见性
            std::string html =
                "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                "<title>" + title + " - 在线OJ</title></head>"
                "<body id=\"oj-msg-page\" style=\"margin:0;font-family:\"Noto Sans SC\",\"PingFang SC\",\"Microsoft YaHei\",sans-serif;"
                "background:#F0F9FF;color:#0C4A6E;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:40px 20px;\">"
                "<div style=\"max-width:520px;width:100%;background:#fff;border:1px solid #E0F2FE;border-radius:18px;"
                "box-shadow:0 8px 30px rgba(14,165,233,0.12);padding:48px 40px;text-align:center;\">"
                "<h2 style=\"font-family:\"Noto Serif SC\",\"Songti SC\",serif;color:#0C4A6E;letter-spacing:1px;margin:0 0 12px;\">" + title + "</h2>"
                "<p style=\"color:#64748B;margin:0 0 28px;line-height:1.8;\">" + message + "</p>"
                "<div style=\"display:flex;gap:12px;justify-content:center;flex-wrap:wrap;\">"
                "<a href=\"/\" style=\"display:inline-block;padding:10px 24px;border-radius:12px;background:#fff;color:#0369A1;border:1px solid #E0F2FE;text-decoration:none;\">返回首页</a>"
                "<a href=\"/login\" style=\"display:inline-block;padding:10px 24px;border-radius:12px;background:#0EA5E9;color:#fff;text-decoration:none;\">去登录</a>"
                "<a href=\"/register\" style=\"display:inline-block;padding:10px 24px;border-radius:12px;background:#fff;color:#0369A1;border:1px solid #E0F2FE;text-decoration:none;\">去注册</a>"
                "</div></div>"
                "<script>try{var t=localStorage.getItem('oj_token');if(t&&!sessionStorage.getItem('oj_msg_retry')){"
                "sessionStorage.setItem('oj_msg_retry','1');"
                "fetch(location.href,{headers:{'Authorization':'Bearer '+t}}).then(function(r){return r.text();}).then(function(h){"
                "sessionStorage.removeItem('oj_msg_retry');"
                "if(h.indexOf('id=\"oj-msg-page\"')===-1){document.open();document.write(h);document.close();}"
                "}).catch(function(){sessionStorage.removeItem('oj_msg_retry');});}}catch(e){}</script>"
                "</body></html>";
            return html;
        }

        // POST /api/questions —— 发布题目(管理员全局 / 负责人组内)
        void AddQuestion(const std::string& auth,const std::string& in_json,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "admin" && cur._role != "leader")
            {
                out["ok"] = false;
                out["message"] = "需要管理员或负责人权限";
                *out_json = WriteJson(out);
                return;
            }
            Json::Reader reader;
            Json::Value in;
            reader.parse(in_json,in);
            Question q;
            std::string err;
            if(!ParseQuestionJson(in,&q,&err))
            {
                out["ok"] = false;
                out["message"] = err;
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role == "leader" && !LeaderOwnsGroup(cur._id,std::atoi(q._scope.c_str())))
            {
                out["ok"] = false;
                out["message"] = "负责人仅能向自己的小组发布题目";
                *out_json = WriteJson(out);
                return;
            }
            if(!_model.AddQuestion(&q))
            {
                out["ok"] = false;
                out["message"] = "发布题目失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["id"] = q._id;
            *out_json = WriteJson(out);
        }

        // PUT /api/questions/{id} —— 修改题目(负责人仅限本组)
        void UpdateQuestion(const std::string& auth,const std::string& id,const std::string& in_json,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "admin" && cur._role != "leader")
            {
                out["ok"] = false;
                out["message"] = "需要管理员或负责人权限";
                *out_json = WriteJson(out);
                return;
            }
            Question old;
            _model.GetOneQuestion(id,&old);
            if(old._id.empty())
            {
                out["ok"] = false;
                out["message"] = "题目不存在";
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role == "leader" && !LeaderOwnsGroup(cur._id,std::atoi(old._scope.c_str())))
            {
                out["ok"] = false;
                out["message"] = "负责人仅能修改本组题目";
                *out_json = WriteJson(out);
                return;
            }
            Json::Reader reader;
            Json::Value in;
            reader.parse(in_json,in);
            Question q;
            std::string err;
            if(!ParseQuestionJson(in,&q,&err))
            {
                out["ok"] = false;
                out["message"] = err;
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role == "leader" && !LeaderOwnsGroup(cur._id,std::atoi(q._scope.c_str())))
            {
                out["ok"] = false;
                out["message"] = "负责人仅能向自己的小组发布题目";
                *out_json = WriteJson(out);
                return;
            }
            q._id = id;
            if(!_model.UpdateQuestion(q))
            {
                out["ok"] = false;
                out["message"] = "修改题目失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["message"] = "题目已修改";
            *out_json = WriteJson(out);
        }

        // DELETE /api/questions/{id} —— 删除题目(负责人仅限本组)
        void DeleteQuestion(const std::string& auth,const std::string& id,std::string* out_json)
        {
            Json::Value out;
            oj_user_model::User cur;
            if(!RequireUser(auth,&cur,&out))
            {
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role != "admin" && cur._role != "leader")
            {
                out["ok"] = false;
                out["message"] = "需要管理员或负责人权限";
                *out_json = WriteJson(out);
                return;
            }
            Question old;
            _model.GetOneQuestion(id,&old);
            if(old._id.empty())
            {
                out["ok"] = false;
                out["message"] = "题目不存在";
                *out_json = WriteJson(out);
                return;
            }
            if(cur._role == "leader" && !LeaderOwnsGroup(cur._id,std::atoi(old._scope.c_str())))
            {
                out["ok"] = false;
                out["message"] = "负责人仅能删除本组题目";
                *out_json = WriteJson(out);
                return;
            }
            if(!_model.DeleteQuestion(id))
            {
                out["ok"] = false;
                out["message"] = "删除题目失败";
                *out_json = WriteJson(out);
                return;
            }
            out["ok"] = true;
            out["message"] = "题目已删除";
            *out_json = WriteJson(out);
        }

        // GET /question_manage —— 题目管理列表页(管理员见全部, 负责人仅见本组)
        void QuestionManage(const std::string& auth,std::string* html)
        {
            oj_user_model::User cur;
            if(!GetSessionUser(auth,&cur) || (cur._role != "admin" && cur._role != "leader"))
            {
                *html = MessagePage("无法访问","需要管理员或负责人登录后才能管理题目。");
                return;
            }

            std::vector<Question> all;
            _model.GetAllQuestions(&all);

            std::vector<oj_user_model::Group> groups;
            _user_model.GetAllGroups(&groups);
            std::unordered_map<std::string,std::string> scope_labels;
            scope_labels["global"] = "全局";
            for(const auto& g : groups)
                scope_labels[std::to_string(g._id)] = g._name;

            std::vector<Question> visible;
            if(cur._role == "admin")
            {
                visible = all;
            }
            else
            {
                std::vector<int> my_gids;
                for(const auto& g : groups)
                    if(g._owner_id == cur._id)
                        my_gids.push_back(g._id);
                for(const auto& q : all)
                {
                    int gid = std::atoi(q._scope.c_str());
                    if(gid > 0 && std::find(my_gids.begin(),my_gids.end(),gid) != my_gids.end())
                        visible.push_back(q);
                }
            }
            std::sort(visible.begin(),visible.end(),[](const Question& a,const Question& b){
                return std::atoll(a._id.c_str()) < std::atoll(b._id.c_str());
            });

            _view.QuestionManageExpandHtml(visible,scope_labels,html);
        }

        // GET /question_manage/edit 与 /question_manage/edit/{id} —— 新增/编辑题目表单页
        void QuestionEdit(const std::string& auth,const std::string& id,std::string* html)
        {
            oj_user_model::User cur;
            if(!GetSessionUser(auth,&cur) || (cur._role != "admin" && cur._role != "leader"))
            {
                *html = MessagePage("无法访问","需要管理员或负责人登录后才能管理题目。");
                return;
            }

            Question q;
            q._rank = Question::EASY;
            q._cpu_limit = 1;
            q._mem_limit = 30;
            std::string qid = id;
            if(!id.empty())
            {
                _model.GetOneQuestion(id,&q);
                if(q._id.empty())
                {
                    *html = MessagePage("题目不存在","该题目不存在或已被删除。");
                    return;
                }
                if(cur._role == "leader" && !LeaderOwnsGroup(cur._id,std::atoi(q._scope.c_str())))
                {
                    *html = MessagePage("无法访问","负责人仅能管理本组题目。");
                    return;
                }
            }

            std::vector<oj_view::ScopeOption> options;
            if(cur._role == "admin")
            {
                options.push_back({"global","全局",q._scope == "global"});
                std::vector<oj_user_model::Group> groups;
                _user_model.GetAllGroups(&groups);
                for(const auto& g : groups)
                    options.push_back({std::to_string(g._id),"小组"+std::to_string(g._id)+"·"+g._name,q._scope == std::to_string(g._id)});
            }
            else
            {
                std::vector<oj_user_model::Group> groups;
                _user_model.GetGroupsByOwner(cur._id,&groups);
                if(groups.empty())
                {
                    *html = MessagePage("无法发布","你还没有小组，请先创建小组后再发布组内题目。");
                    return;
                }
                for(const auto& g : groups)
                    options.push_back({std::to_string(g._id),"小组"+std::to_string(g._id)+"·"+g._name,q._scope == std::to_string(g._id)});
            }
            _view.QuestionEditExpandHtml(q,qid,options,html);
        }

    private:
        Model _model;
        oj_view::View _view;
        LoadBlance _loadblance;
        oj_user_model::UserModel _user_model;
    };
}