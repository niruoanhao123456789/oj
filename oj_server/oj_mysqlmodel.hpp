#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <fstream>
#include <unordered_map>
#include <mysql/mysql.h>
#include "../common/log/Log.hpp"
#include "../common/Util.hpp"

namespace oj_mysqlmodel
{
    using namespace LogModule;
    using namespace oj_util;

    struct Question
    {
        enum Rank
        {
            EASY,
            NORMAL,
            DIFFICULT,
            UNKONW
        };

        static Rank StringToRank(const std::string& rank)
        {
            if(rank == "简单")
                return Rank::EASY;
            else if(rank == "中等")
                return Rank::NORMAL;
            else if(rank == "困难")
                return Rank::DIFFICULT;
            else
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","the rank string is undefined!");
                return Rank::UNKONW;
            }
        }

        const std::string RankToString() const
        {
            switch(_rank)
            {
                case EASY:      return "简单";
                case NORMAL:    return "中等";
                case DIFFICULT: return "困难";
                default:        return "未知";
            }
        }

        std::string _id;        // 题目编号，唯一
        std::string _title;     // 题目的标题
        Rank _rank;             // 难度: 简单 中等 困难
        size_t _cpu_limit;      // 题目的时间要求(S)
        size_t _mem_limit;      // 题目的空间要去(MB)
        std::string _desc;      // 题目的描述
        std::string _header;    // 隐藏头文件(函数接口模式), 拼接在用户代码之前
        std::string _answer;    // 在线编辑器预填代码: 函数模式只放待实现函数/类, ACM 模式含 main
        std::string _tail;      // 隐藏附加代码: 函数模式放读 stdin 调 answer 的 main 驱动, ACM 留空
        std::string _scope = "global";  // 可见范围: "global" 表示全局题, 否则为小组id
        std::string _mode = "acm";      // 出题模式: 'function' 函数接口 / 'acm' 传统ACM IO
        std::string _visible_cases;     // 显式样例 JSON 数组字符串(答题页展示, 不判题)
        std::string _hidden_cases;      // 隐藏判题用例 JSON 数组字符串(判题唯一依据, 不展示)
    };

    const std::string database = "oj";
    const std::string table = "questions";
    const std::string host = "127.0.0.1";
    const std::string user = "oj_client";
    const std::string password = "1234";
    const size_t port = 3306;
    

    class Model
    {
    public:
        void QueryMysql(const std::string& sql, std::vector<oj_mysqlmodel::Question>* out)
        {
            assert(out);
            // 连接数据库
            MYSQL* my = Connect();
            if(my == nullptr)
                return;

            // 执行sql语句
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","execute error!");
                mysql_close(my);
                return;
            }

            // 提取结果
            MYSQL_RES* res = mysql_store_result(my);
            if(res == nullptr)
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","store result failed!");
                mysql_close(my);
                return;
            }

            // 分析结果
            size_t rows = mysql_num_rows(res);
            size_t cols = mysql_num_fields(res);

            oj_mysqlmodel::Question q;
            for(size_t i=0;i<rows;i++)
            {
                MYSQL_ROW row = mysql_fetch_row(res);
                q._id = row[0];
                q._title = row[1];
                q._rank = oj_mysqlmodel::Question::StringToRank(row[2]);
                q._desc = row[3];
                q._header = row[4];
                q._answer = row[5];
                q._tail = row[6];
                q._cpu_limit = std::atoll(row[7]);
                q._mem_limit = std::atoll(row[8]);
                // 兼容旧表(未迁移scope列)的情况
                q._scope = (cols > 9 && row[9]) ? row[9] : "global";
                // mode / visible_cases / hidden_cases (旧表缺省)
                q._mode = (cols > 10 && row[10]) ? row[10] : "acm";
                q._visible_cases = (cols > 11 && row[11]) ? row[11] : "";
                q._hidden_cases = (cols > 12 && row[12]) ? row[12] : "";

                out->emplace_back(q);
            }
            // 释放结果空间
            mysql_free_result(res);
            // 关闭mysql连接
            mysql_close(my);
        }

        void GetAllQuestions(std::vector<oj_mysqlmodel::Question>* out)
        {
            assert(out);
            std::string sql = "select * from ";
            sql += table;
            QueryMysql(sql,out);
        }

        void GetOneQuestion(const std::string& id,oj_mysqlmodel::Question* q)
        {
            assert(q);
            std::string sql = "select * from " + table + " where id=" + id;
            std::vector<oj_mysqlmodel::Question> ret;
            QueryMysql(sql,&ret);
            if(ret.size()==1)
            {
                *q = ret[0];
            }
        }

        // 执行无返回结果的sql语句(建表/改表/普通DML), 成功返回true
        bool ExecuteSql(const std::string& sql)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","execute sql failed!");
                mysql_close(my);
                return false;
            }
            mysql_close(my);
            return true;
        }

        // 新增题目: 转义 + INSERT, 通过 mysql_insert_id 回填题目id
        bool AddQuestion(oj_mysqlmodel::Question* q)
        {
            assert(q);
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            std::string title = Escape(my,q->_title);
            std::string rank = Escape(my,q->RankToString());
            std::string desc = Escape(my,q->_desc);
            std::string header = Escape(my,q->_header);
            std::string answer = Escape(my,q->_answer);
            std::string tail = Escape(my,q->_tail);
            std::string scope = Escape(my,q->_scope);
            std::string mode = Escape(my,q->_mode);
            std::string vcases = Escape(my,q->_visible_cases);
            std::string hcases = Escape(my,q->_hidden_cases);

            std::string sql = "insert into " + table +
                " (title,`rank`,desc_text,header,answer,tail,cpu_limit,mem_limit,scope,mode,visible_cases,hidden_cases) values ('" +
                title + "','" + rank + "','" + desc + "','" + header + "','" + answer + "','" + tail + "'," +
                std::to_string(q->_cpu_limit) + "," + std::to_string(q->_mem_limit) + ",'" + scope + "','" + mode +
                "','" + vcases + "','" + hcases + "')";

            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","add question failed!");
                mysql_close(my);
                return false;
            }
            q->_id = std::to_string(mysql_insert_id(my));
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s","add question succeed! id: ",q->_id.c_str());
            mysql_close(my);
            return true;
        }

        // 修改题目
        bool UpdateQuestion(const oj_mysqlmodel::Question& q)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            std::string title = Escape(my,q._title);
            std::string rank = Escape(my,q.RankToString());
            std::string desc = Escape(my,q._desc);
            std::string header = Escape(my,q._header);
            std::string answer = Escape(my,q._answer);
            std::string tail = Escape(my,q._tail);
            std::string scope = Escape(my,q._scope);
            std::string mode = Escape(my,q._mode);
            std::string vcases = Escape(my,q._visible_cases);
            std::string hcases = Escape(my,q._hidden_cases);

            std::string sql = "update " + table + " set title='" + title + "',`rank`='" + rank +
                "',desc_text='" + desc + "',header='" + header + "',answer='" + answer +
                "',tail='" + tail + "',cpu_limit=" + std::to_string(q._cpu_limit) +
                ",mem_limit=" + std::to_string(q._mem_limit) + ",scope='" + scope +
                "',mode='" + mode + "',visible_cases='" + vcases + "',hidden_cases='" + hcases +
                "' where id=" + q._id;

            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","update question failed!");
                mysql_close(my);
                return false;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s","update question succeed! id: ",q._id.c_str());
            mysql_close(my);
            return true;
        }

        // 删除题目
        bool DeleteQuestion(const std::string& id)
        {
            MYSQL* my = Connect();
            if(my == nullptr)
                return false;

            std::string sql = "delete from " + table + " where id=" + id;
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","delete question failed!");
                mysql_close(my);
                return false;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s","delete question succeed! id: ",id.c_str());
            mysql_close(my);
            return true;
        }

    private:
        // 建立并返回已连接、设置好编码的mysql句柄, 失败返回nullptr
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

            // 设置该链接的编码格式, 避免出现乱码问题
            mysql_set_character_set(my,"utf-8");

            LOG_INFOR(GetLogger("oj_Logger"),"%s","connect database succeed");
            return my;
        }

        // 使用 mysql_real_escape_string 转义字符串, 防注入
        static std::string Escape(MYSQL* my, const std::string& src)
        {
            std::string dst;
            dst.resize(2 * src.size() + 1);
            unsigned long len = mysql_real_escape_string(my,&dst[0],src.c_str(),src.size());
            dst.resize(len);
            return dst;
        }
    };
}