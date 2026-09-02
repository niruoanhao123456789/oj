#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>
#include <mysql/mysql.h>
#include "../../common/log/Log.hpp"
#include "../../common/Util.hpp"
#include "oj_passwd.hpp"

namespace oj_user_model
{
    using namespace LogModule;
    using namespace oj_util;

    struct User
    {
        int _id = 0;
        std::string _username;
        std::string _password_hash;
        std::string _salt;
        std::string _role;
        std::string _created_at;
    };

    struct Group
    {
        int _id = 0;
        std::string _name;
        int _owner_id = 0;
        std::string _invite_code;
        std::string _created_at;
    };

    const std::string database = "oj";
    const std::string user_table = "users";
    const std::string group_table = "`groups`";
    const std::string member_table = "group_members";
    const std::string admin_invite_table = "admin_invite";
    const std::string host = "127.0.0.1";
    const std::string user = "oj_client";
    const std::string password = "1234";
    const size_t port = 3306;

    class UserModel
    {
    public:
        // ---------- 密码 ----------
        // 加盐值 = 注册时的时间戳
        static std::string GenerateSalt()
        {
            return Time::GetTimeStamp();
        }

        static std::string HashPassword(const std::string& password,const std::string& salt)
        {
            return oj_passwd::HashPassword(password,salt);
        }

        // ---------- 注册 / 登录 ----------
        // 注册: 首个用户自动成为 admin; 否则按请求的 role(user/leader) 注册;
        // leader 身份需先通过 IsValidAdminInviteCode 校验; out_role 回填实际角色
        bool Register(const std::string& username,const std::string& password,const std::string& role,std::string* out_role)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            int count = 0;
            if(!UserCount(&count))
            {
                mysql_close(my);
                return false;
            }
            std::string final_role = (count == 0) ? "admin" : role;

            std::string salt = GenerateSalt();
            std::string hash = HashPassword(password,salt);

            std::string sql = "insert into " + user_table +
                " (username,password_hash,salt,role,created_at) values ('" +
                Escape(my,username) + "','" + hash + "','" + salt + "','" + final_role + "',NOW())";

            if(mysql_query(my,sql.c_str()))
            {
                // 1062: username 唯一键冲突
                LOG_WARNNING(GetLogger("oj_Logger"),"%s%s","register failed! username: ",username.c_str());
                mysql_close(my);
                return false;
            }
            if(out_role)
                *out_role = final_role;
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s%s","register succeed! username: ",username.c_str(),(" role: " + final_role).c_str());
            mysql_close(my);
            return true;
        }

        // ---------- 管理员邀请码 (注册负责人用, 见 admin_invite 表) ----------
        // 校验 code 是否为当前有效的管理员邀请码
        bool IsValidAdminInviteCode(const std::string& code)
        {
            if(code.empty())
                return false;
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select 1 from " + admin_invite_table +
                " where id=1 and code='" + Escape(my,code) + "'";
            bool ok = false;
            if(mysql_query(my,sql.c_str()) == 0)
            {
                MYSQL_RES* res = mysql_store_result(my);
                if(res != nullptr)
                {
                    ok = (mysql_num_rows(res) == 1);
                    mysql_free_result(res);
                }
            }
            mysql_close(my);
            return ok;
        }

        // 生成/重置管理员邀请码(固定单行 id=1), 旧码立即失效
        bool ResetAdminInvite(std::string* code)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            std::string new_code = RandomHex(12);
            std::string sql = "insert into " + admin_invite_table +
                " (id,code,created_at) values (1,'" + new_code + "',NOW()) on duplicate key update code='" +
                new_code + "',created_at=NOW()";
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","reset admin invite failed!");
                mysql_close(my);
                return false;
            }
            if(code)
                *code = new_code;
            LOG_INFOR(GetLogger("oj_Logger"),"%s","reset admin invite succeed!");
            mysql_close(my);
            return true;
        }

        // 登录校验: 用相同盐重算哈希比对
        bool Login(const std::string& username,const std::string& password,User* out)
        {
            if(!GetUserByName(username,out))
                return false;
            if(out->_password_hash != HashPassword(password,out->_salt))
                return false;
            return true;
        }

        bool GetUserByName(const std::string& username,User* out)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select * from " + user_table + " where username='" + Escape(my,username) + "'";
            bool ok = FetchUser(my,sql,out);
            mysql_close(my);
            return ok;
        }

        bool GetUserById(int id,User* out)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select * from " + user_table + " where id=" + std::to_string(id);
            bool ok = FetchUser(my,sql,out);
            mysql_close(my);
            return ok;
        }

        bool UserCount(int* count)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            if(mysql_query(my,("select count(*) from " + user_table).c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","count users failed!");
                mysql_close(my);
                return false;
            }
            MYSQL_RES* res = mysql_store_result(my);
            if(res == nullptr)
            {
                mysql_close(my);
                return false;
            }
            MYSQL_ROW row = mysql_fetch_row(res);
            *count = row ? std::atoi(row[0]) : 0;
            mysql_free_result(res);
            mysql_close(my);
            return true;
        }

        // 修改用户角色等级 (admin / leader / user)
        bool UpdateRole(int id,const std::string& role)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "update " + user_table + " set role='" + role + "' where id=" + std::to_string(id);
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","update role failed!");
                mysql_close(my);
                return false;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s%d%s%s","update role succeed! id: ",id," role: ",role.c_str());
            mysql_close(my);
            return true;
        }

        // ---------- 小组 ----------
        bool CreateGroup(const std::string& name,int owner_id,Group* out)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            bool ok = false;
            // 邀请码需唯一, 冲突则换码重试
            for(int attempt = 0;attempt < 5 && !ok;++attempt)
            {
                std::string code = RandomHex(12);
                std::string sql = "insert into " + group_table +
                    " (name,owner_id,invite_code,created_at) values ('" +
                    Escape(my,name) + "'," + std::to_string(owner_id) + ",'" + code + "',NOW())";
                if(mysql_query(my,sql.c_str()) == 0)
                {
                    if(out)
                    {
                        out->_id = (int)mysql_insert_id(my);
                        out->_name = name;
                        out->_owner_id = owner_id;
                        out->_invite_code = code;
                    }
                    ok = true;
                }
                else
                {
                    LOG_WARNNING(GetLogger("oj_Logger"),"%s","create group failed, retry with new invite code!");
                }
            }
            if(!ok)
            {
                mysql_close(my);
                return false;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s%d%s%s","create group succeed! owner_id: ",owner_id," invite_code: ",(out ? out->_invite_code : "").c_str());
            mysql_close(my);
            return true;
        }

        bool GetGroupById(int id,Group* out)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select * from " + group_table + " where id=" + std::to_string(id);
            bool ok = FetchGroup(my,sql,out);
            mysql_close(my);
            return ok;
        }

        bool GetGroupByOwner(int owner_id,Group* out)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select * from " + group_table + " where owner_id=" + std::to_string(owner_id);
            bool ok = FetchGroup(my,sql,out);
            mysql_close(my);
            return ok;
        }

        // 某用户拥有的所有小组(负责人可创建多个)
        bool GetGroupsByOwner(int owner_id,std::vector<Group>* out)
        {
            assert(out);
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select * from " + group_table + " where owner_id=" + std::to_string(owner_id);
            bool ok = FetchGroups(my,sql,out);
            mysql_close(my);
            return ok;
        }

        // 所有小组(管理员发布组内题时使用)
        bool GetAllGroups(std::vector<Group>* out)
        {
            assert(out);
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select * from " + group_table;
            bool ok = FetchGroups(my,sql,out);
            mysql_close(my);
            return ok;
        }

        bool GetGroupByInvite(const std::string& code,Group* out)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select * from " + group_table + " where invite_code='" + Escape(my,code) + "'";
            bool ok = FetchGroup(my,sql,out);
            mysql_close(my);
            return ok;
        }

        // 重置邀请码, 旧码失效
        bool ResetInviteCode(int group_id,std::string* code)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            bool ok = false;
            for(int attempt = 0;attempt < 5 && !ok;++attempt)
            {
                std::string new_code = RandomHex(12);
                std::string sql = "update " + group_table + " set invite_code='" + new_code +
                    "' where id=" + std::to_string(group_id);
                if(mysql_query(my,sql.c_str()) == 0)
                {
                    if(code)
                        *code = new_code;
                    ok = true;
                }
                else
                {
                    LOG_WARNNING(GetLogger("oj_Logger"),"%s","reset invite code failed, retry!");
                }
            }
            if(!ok)
            {
                mysql_close(my);
                return false;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s%d","reset invite code succeed! group_id: ",group_id);
            mysql_close(my);
            return true;
        }

        // 删除小组: 先删除成员关系, 再删除组内题目(scope=小组id), 最后删除小组本身
        bool DeleteGroup(int group_id)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            std::string del_members = "delete from " + member_table + " where group_id=" + std::to_string(group_id);
            if(mysql_query(my,del_members.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s%d","delete group members failed! group_id: ",group_id);
                mysql_close(my);
                return false;
            }
            std::string del_questions = "delete from questions where scope='" + std::to_string(group_id) + "'";
            if(mysql_query(my,del_questions.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s%d","delete group questions failed! group_id: ",group_id);
                mysql_close(my);
                return false;
            }
            std::string del_group = "delete from " + group_table + " where id=" + std::to_string(group_id);
            if(mysql_query(my,del_group.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s%d","delete group failed! group_id: ",group_id);
                mysql_close(my);
                return false;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s%d","delete group succeed! group_id: ",group_id);
            mysql_close(my);
            return true;
        }

        // 加入小组
        bool JoinGroup(int user_id,int group_id)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "insert into " + member_table + " (group_id,user_id) values (" +
                std::to_string(group_id) + "," + std::to_string(user_id) + ")";
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","join group failed!");
                mysql_close(my);
                return false;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s%d%s%d","join group succeed! user_id: ",user_id," group_id: ",group_id);
            mysql_close(my);
            return true;
        }

        bool IsMember(int user_id,int group_id)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select count(*) from " + member_table +
                " where group_id=" + std::to_string(group_id) + " and user_id=" + std::to_string(user_id);
            int count = 0;
            if(mysql_query(my,sql.c_str()))
            {
                mysql_close(my);
                return false;
            }
            MYSQL_RES* res = mysql_store_result(my);
            if(res == nullptr)
            {
                mysql_close(my);
                return false;
            }
            MYSQL_ROW row = mysql_fetch_row(res);
            count = row ? std::atoi(row[0]) : 0;
            mysql_free_result(res);
            mysql_close(my);
            return count > 0;
        }

        // 用户所属的所有小组 id
        bool GetUserGroups(int user_id,std::vector<int>* out)
        {
            assert(out);
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;
            std::string sql = "select group_id from " + member_table + " where user_id=" + std::to_string(user_id);
            if(mysql_query(my,sql.c_str()))
            {
                mysql_close(my);
                return false;
            }
            MYSQL_RES* res = mysql_store_result(my);
            if(res == nullptr)
            {
                mysql_close(my);
                return false;
            }
            int rows = mysql_num_rows(res);
            for(int i = 0;i < rows;++i)
            {
                MYSQL_ROW row = mysql_fetch_row(res);
                out->push_back(row ? std::atoi(row[0]) : 0);
            }
            mysql_free_result(res);
            mysql_close(my);
            return true;
        }

        // ---------- 内存会话(token) ----------
        std::string CreateSession(const User& user)
        {
            std::string token = RandomHex(16);
            std::lock_guard<std::mutex> lock(_session_mutex);
            _sessions[token] = user;
            return token;
        }

        bool GetSession(const std::string& token,User* out)
        {
            std::lock_guard<std::mutex> lock(_session_mutex);
            auto iter = _sessions.find(token);
            if(iter == _sessions.end())
                return false;
            *out = iter->second;
            return true;
        }

    private:
        MYSQL* Connect()
        {
            MYSQL* my = mysql_init(nullptr);
            if(my == nullptr || mysql_real_connect(my,host.c_str(),user.c_str(),password.c_str(),database.c_str(),port,nullptr,0) == nullptr)
            {
                LOG_FATAL(GetLogger("oj_Logger"),"%s","connect database failed!");
                if(my)
                    mysql_close(my);
                return nullptr;
            }
            mysql_set_character_set(my,"utf-8");
            return my;
        }

        static std::string Escape(MYSQL* my,const std::string& src)
        {
            std::string dst;
            dst.resize(2 * src.size() + 1);
            unsigned long len = mysql_real_escape_string(my,&dst[0],src.c_str(),src.size());
            dst.resize(len);
            return dst;
        }

        // 生成 n 字节随机数的十六进制字符串
        static std::string RandomHex(size_t nbytes)
        {
            static std::random_device rd;
            static const char* hex = "0123456789abcdef";
            std::string out;
            out.reserve(nbytes * 2);
            for(size_t i = 0;i < nbytes;++i)
            {
                unsigned char b = (unsigned char)(rd() & 0xFF);
                out += hex[b >> 4];
                out += hex[b & 0x0F];
            }
            return out;
        }

        // 执行查询并取出唯一一条用户记录, 成功返回 true
        static bool FetchUser(MYSQL* my,const std::string& sql,User* out)
        {
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","query user failed!");
                return false;
            }
            MYSQL_RES* res = mysql_store_result(my);
            if(res == nullptr)
                return false;
            int rows = mysql_num_rows(res);
            if(rows != 1)
            {
                mysql_free_result(res);
                return false;
            }
            MYSQL_ROW row = mysql_fetch_row(res);
            out->_id = std::atoi(row[0]);
            out->_username = row[1] ? row[1] : "";
            out->_password_hash = row[2] ? row[2] : "";
            out->_salt = row[3] ? row[3] : "";
            out->_role = row[4] ? row[4] : "";
            out->_created_at = row[5] ? row[5] : "";
            mysql_free_result(res);
            return true;
        }

        // 执行查询并取出唯一一条小组记录, 成功返回 true
        static bool FetchGroup(MYSQL* my,const std::string& sql,Group* out)
        {
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","query group failed!");
                return false;
            }
            MYSQL_RES* res = mysql_store_result(my);
            if(res == nullptr)
                return false;
            int rows = mysql_num_rows(res);
            if(rows != 1)
            {
                mysql_free_result(res);
                return false;
            }
            MYSQL_ROW row = mysql_fetch_row(res);
            out->_id = std::atoi(row[0]);
            out->_name = row[1] ? row[1] : "";
            out->_owner_id = std::atoi(row[2]);
            out->_invite_code = row[3] ? row[3] : "";
            out->_created_at = row[4] ? row[4] : "";
            mysql_free_result(res);
            return true;
        }

        // 多行版 FetchGroup
        static bool FetchGroups(MYSQL* my,const std::string& sql,std::vector<Group>* out)
        {
            assert(out);
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","query groups failed!");
                return false;
            }
            MYSQL_RES* res = mysql_store_result(my);
            if(res == nullptr)
                return false;
            int rows = mysql_num_rows(res);
            for(int i = 0;i < rows;++i)
            {
                MYSQL_ROW row = mysql_fetch_row(res);
                Group g;
                g._id = row[0] ? std::atoi(row[0]) : 0;
                g._name = row[1] ? row[1] : "";
                g._owner_id = row[2] ? std::atoi(row[2]) : 0;
                g._invite_code = row[3] ? row[3] : "";
                g._created_at = row[4] ? row[4] : "";
                out->push_back(g);
            }
            mysql_free_result(res);
            return true;
        }

        std::unordered_map<std::string,User> _sessions;
        std::mutex _session_mutex;
    };
}
