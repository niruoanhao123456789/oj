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

    public:
        void AllExpandHtml(const std::vector<Question>& questions,std::string *html)
        {
            // 题目的编号 题目的标题 题目的难度
            // 推荐使用表格显示
            // 1、形成路径
            std::string src_html = template_path + "all_questions.html";
            // 2、形成数字字典
            ctemplate::TemplateDictionary root("all_questions");
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

        void OneExpandHtml(Question& q, std::string* html)
        {
            std::string src_html = template_path + "one_question.html";

            ctemplate::TemplateDictionary root("one_question");
            root.SetValue("id",q._id);
            root.SetValue("title",q._title);
            root.SetValue("rank",q.RankToString());
            root.SetValue("desc",q._desc);
            root.SetValue("pre_code",q._answer);

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
                ",\"mem_limit\":" + std::to_string(q._mem_limit) + "}";
            root.SetValue("qdata",qdata);

            root.SetValue("sel_simple",   q._rank == Question::EASY      ? "selected" : "");
            root.SetValue("sel_normal",   q._rank == Question::NORMAL    ? "selected" : "");
            root.SetValue("sel_difficult",q._rank == Question::DIFFICULT ? "selected" : "");

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
    };
}