#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <ctemplate/template.h>
#include "oj_filemodel.hpp"
#include "oj_mysqlmodel.hpp"

namespace oj_view
{
    using namespace oj_mysqlmodel;
    // using namespace oj_filemodel;

    const std::string template_path = "./template_html/";

    // 题目编辑页的可见范围选项(全局/小组)
    struct ScopeOption
    {
        std::string value;      // "global" 或小组id
        std::string label;      // 展示文本
        bool selected;
    };

    // 小组管理页中的小组条目
    struct GroupEntry
    {
        std::string id;
        std::string name;
        std::string invite_code;
        std::string created_at;
    };

    class View
    {
    private:
        // 将字符串转义为可安全嵌入 JSON 字符串字面量
        static std::string EscapeJson(const std::string& s)
        {
            std::string out;
            out.reserve(s.size() * 2);
            for(size_t i = 0;i < s.size();++i)
            {
                unsigned char c = (unsigned char)s[i];
                if(c == '<' && i + 1 < s.size() && s[i+1] == '/')
                {
                    out += "\\/";   // 避免 </script> 提前截断
                    ++i;
                    continue;
                }
                switch(c)
                {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if(c < 0x20)
                        {
                            char buf[8];
                            std::snprintf(buf,sizeof(buf),"\\u%04x",c);
                            out += buf;
                        }
                        else
                        {
                            out += (char)c;
                        }
                }
            }
            return out;
        }

        // 将已保存的用例 JSON 数组文本安全嵌入 <script>(避免 </script> 截断)
        static std::string SafeArrayJson(const std::string& json)
        {
            if(json.empty())
                return "[]";
            std::string out;
            out.reserve(json.size());
            for(size_t i = 0;i < json.size();++i)
            {
                if(json[i] == '<' && i + 1 < json.size() && json[i+1] == '/')
                {
                    out += "<\\/";  // 保持合法JSON转义, 同时防截断
                    ++i;
                    continue;
                }
                out += json[i];
            }
            return out;
        }

    public:
        void AllExpandHtml(const std::vector<Question>& questions,bool logged_in,std::string *html)
        {
            // 题目的编号 题目的标题 题目的难度
            // 推荐使用表格显示
            // 1、形成路径
            std::string src_html = template_path + "all_questions.html";
            // 2、形成数字字典
            ctemplate::TemplateDictionary root("all_questions");
            // 该页面是否已按登录态渲染(供前端判断是否需携带 token 重新拉取)
            root.SetValue("logged_in_flag",logged_in ? "1" : "0");
            for(const auto& q:questions)
            {
                ctemplate::TemplateDictionary* sub = root.AddSectionDictionary("question_list");
                sub->SetValue("id",q._id);
                sub->SetValue("title",q._title);
                sub->SetValue("rank",q.RankToString());
            }

            // 3、获取被渲染的html
            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);

            // 4、开始完成渲染功能
            tpl->Expand(html,&root);
        }

        void OneExpandHtml(Question& q,bool logged_in,std::string* html)
        {
            std::string src_html = template_path + "one_question.html";

            ctemplate::TemplateDictionary root("one_question");
            root.SetValue("id",q._id);
            root.SetValue("title",q._title);
            root.SetValue("rank",q.RankToString());
            root.SetValue("desc",q._desc);
            root.SetValue("pre_code",q._answer);
            root.SetValue("samples",SafeArrayJson(q._visible_cases));  // 显式样例(不判题), 答题页展示
            root.SetValue("mode",q._mode);
            root.SetValue("logged_in_flag",logged_in ? "1" : "0");

            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);

            tpl->Expand(html,&root);
        }

        // 题目管理列表页 (管理员见全部题, 负责人见本组题; 过滤在 Control 层完成)
        void QuestionManageExpandHtml(const std::vector<Question>& questions,const std::unordered_map<std::string,std::string>& scope_labels,std::string* html)
        {
            std::string src_html = template_path + "question_manage.html";
            ctemplate::TemplateDictionary root("question_manage");
            for(const auto& q:questions)
            {
                ctemplate::TemplateDictionary* sub = root.AddSectionDictionary("question_list");
                sub->SetValue("id",q._id);
                sub->SetValue("title",q._title);
                sub->SetValue("rank",q.RankToString());
                auto it = scope_labels.find(q._scope);
                sub->SetValue("scope",(it == scope_labels.end() ? q._scope : it->second));
            }
            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);
            if(tpl)
                tpl->Expand(html,&root);
        }

        // 新增/编辑题目共用表单页 (qid 为空表示新增; options 为可见范围下拉)
        void QuestionEditExpandHtml(const Question& q,const std::string& qid,const std::vector<ScopeOption>& options,std::string* html)
        {
            std::string src_html = template_path + "question_edit.html";
            ctemplate::TemplateDictionary root("question_edit");
            root.SetValue("qid",qid);
            root.SetValue("page_title",qid.empty() ? "新增题目" : "编辑题目 #" + qid);

            std::string qdata = "{\"id\":\"" + EscapeJson(qid) + "\",\"title\":\"" + EscapeJson(q._title) +
                "\",\"rank\":\"" + EscapeJson(q.RankToString()) + "\",\"desc\":\"" + EscapeJson(q._desc) +
                "\",\"header\":\"" + EscapeJson(q._header) + "\",\"answer\":\"" + EscapeJson(q._answer) +
                "\",\"tail\":\"" + EscapeJson(q._tail) + "\",\"cpu_limit\":" + std::to_string(q._cpu_limit) +
                ",\"mem_limit\":" + std::to_string(q._mem_limit) + ",\"scope\":\"" + EscapeJson(q._scope) +
                "\",\"mode\":\"" + EscapeJson(q._mode) +
                "\",\"visible_cases\":" + SafeArrayJson(q._visible_cases) +
                ",\"hidden_cases\":" + SafeArrayJson(q._hidden_cases) + "}";
            root.SetValue("qdata",qdata);

            // 难度下拉预选: 区块由 AddSectionDictionary 驱动
            if(q._rank == Question::EASY)
                root.AddSectionDictionary("sel_simple");
            if(q._rank == Question::NORMAL)
                root.AddSectionDictionary("sel_normal");
            if(q._rank == Question::DIFFICULT)
                root.AddSectionDictionary("sel_difficult");

            for(const auto& opt:options)
            {
                ctemplate::TemplateDictionary* sub = root.AddSectionDictionary("scope_options");
                sub->SetValue("value",opt.value);
                sub->SetValue("label",opt.label);
                sub->SetValue("selected",opt.selected ? "selected" : "");
            }

            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);
            if(tpl)
                tpl->Expand(html,&root);
        }

        // 注册页(普通用户/负责人, 负责人需管理员邀请码)
        void RegisterExpandHtml(std::string* html)
        {
            std::string src_html = template_path + "register.html";
            ctemplate::TemplateDictionary root("register");
            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);
            if(tpl)
                tpl->Expand(html,&root);
        }

        // 登录页
        void LoginExpandHtml(std::string* html)
        {
            std::string src_html = template_path + "login.html";
            ctemplate::TemplateDictionary root("login");
            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);
            if(tpl)
                tpl->Expand(html,&root);
        }

        // 小组管理页
        // role: admin/leader/user; my_groups: 负责人/管理员所拥有的小组; joined_groups: 普通用户已加入的小组
        void GroupManageExpandHtml(const std::string& role,const std::string& username,
            const std::vector<GroupEntry>& my_groups,const std::vector<GroupEntry>& joined_groups,
            std::string* html)
        {
            std::string src_html = template_path + "group_manage.html";
            ctemplate::TemplateDictionary root("group_manage");
            root.SetValue("role",role);
            root.SetValue("username",username);
            // ctemplate 中区块仅由 AddSectionDictionary 驱动: 调用一次即渲染一次
            if(role == "admin")
                root.AddSectionDictionary("is_admin");
            if(role == "leader" || role == "admin")
                root.AddSectionDictionary("is_leader");
            if(role == "user")
                root.AddSectionDictionary("is_user");

            for(const auto& g : my_groups)
            {
                ctemplate::TemplateDictionary* sub = root.AddSectionDictionary("my_group_list");
                sub->SetValue("id",g.id);
                sub->SetValue("name",g.name);
                sub->SetValue("invite_code",g.invite_code);
                sub->SetValue("created_at",g.created_at);
            }
            for(const auto& g : joined_groups)
            {
                ctemplate::TemplateDictionary* sub = root.AddSectionDictionary("joined_group_list");
                sub->SetValue("id",g.id);
                sub->SetValue("name",g.name);
                sub->SetValue("created_at",g.created_at);
            }

            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);
            if(tpl)
                tpl->Expand(html,&root);
        }
    };
}