#pragma once
#include <iostream>
#include <string>
#include <ctemplate/template.h>
#include "oj_filemodel.hpp"

namespace oj_view
{
    using namespace oj_filemodel;

    const std::string template_path = "./template_html/";

    class View
    {
    public:
        void AllExpandHtml(const std::vector<oj_filemodel::Question>& questions,std::string *html)
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
                root.SetValue("id",q._id);
                root.SetValue("title",q._title);
                root.SetValue("rank",q.RankToString());
            }

            // 3、获取被渲染的html
            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);

            // 4、开始完成渲染功能
            tpl->Expand(html,&root);
        }

        void OneExpandHtml(oj_filemodel::Question& q, std::string* html)
        {
            std::string src_html = template_path + "one_question.html";

            ctemplate::TemplateDictionary root("one_question");
            root.SetValue("id",q._id);
            root.SetValue("title",q._title);
            root.SetValue("rank",q.RankToString());
            root.SetValue("desc",q._desc);
            root.SetValue("pre_code",q._header);

            ctemplate::Template* tpl = ctemplate::Template::GetTemplate(src_html,ctemplate::DO_NOT_STRIP);

            tpl->Expand(html,&root);
        }
    };
}