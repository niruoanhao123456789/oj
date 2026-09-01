#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include "../../oj_server/oj_filemodel.hpp"
#include "test_env.hpp"

namespace fs = std::filesystem;

namespace
{
    void WriteFile(const std::string& path, const std::string& content)
    {
        std::ofstream out(path);
        ASSERT_TRUE(out.is_open()) << "write failed: " << path;
        out << content;
    }

    std::string ReadList()
    {
        std::ifstream in("./questions/questions.list");
        EXPECT_TRUE(in.is_open());
        std::string all, line;
        while(std::getline(in, line))
            all += line + "\n";
        return all;
    }

    void SeedQuestionFiles(const std::string& id)
    {
        fs::create_directories("./questions/" + id);
        WriteFile("./questions/" + id + "/desc.txt",   "desc_" + id);
        WriteFile("./questions/" + id + "/header.cpp", "header_" + id);
        WriteFile("./questions/" + id + "/answer.cpp", "answer_" + id);
        WriteFile("./questions/" + id + "/tail.cpp",   "tail_" + id);
    }

    void SeedList(const std::vector<std::string>& lines)
    {
        std::ofstream out("./questions/questions.list");
        ASSERT_TRUE(out.is_open());
        for(const auto& l : lines)
            out << l << "\n";
        out.close();
    }
}

class FileModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fs::remove_all("./questions");
        fs::create_directories("./questions");
    }
    void TearDown() override
    {
        fs::remove_all("./questions");
    }
};

// 6 列格式 + 兼容旧 5 列(缺省 scope=global) + 跳过非法行
TEST_F(FileModelTest, LoadQuestionsListParsesSixAndFiveColumns)
{
    SeedList({"1 判断回文数 简单 1 30 global", "2 求最大值 中等 2 64", "invalid"});
    SeedQuestionFiles("1");
    SeedQuestionFiles("2");

    oj_filemodel::Model m;
    std::vector<oj_filemodel::Question> out;
    m.GetAllQuestions(&out);
    ASSERT_EQ(out.size(), 2u);

    oj_filemodel::Question q1, q2;
    for(const auto& q : out)
        (q._id == "1" ? q1 : q2) = q;
    EXPECT_EQ(q1._title, "判断回文数");
    EXPECT_EQ(q1._rank, oj_filemodel::Question::EASY);
    EXPECT_EQ(q1._scope, "global");
    EXPECT_EQ(q1._desc, "desc_1\n");   // ReadFile 保留行尾换行

    EXPECT_EQ(q2._title, "求最大值");
    EXPECT_EQ(q2._rank, oj_filemodel::Question::NORMAL);
    EXPECT_EQ(q2._cpu_limit, 2u);
    EXPECT_EQ(q2._mem_limit, 64u);
    EXPECT_EQ(q2._scope, "global");   // 5 列缺省
}

// AddQuestion: 分配 max(id)+1, 创建目录/四文件, 追加 6 列列表行
TEST_F(FileModelTest, AddQuestionAssignsMaxIdPlusOne)
{
    SeedList({"1 判断回文数 简单 1 30 global"});
    SeedQuestionFiles("1");

    oj_filemodel::Model m;
    oj_filemodel::Question q;
    q._title = "新增题";
    q._rank = oj_filemodel::Question::NORMAL;
    q._desc = "new_desc";
    q._header = "new_header";
    q._answer = "new_answer";
    q._tail = "new_tail";
    q._cpu_limit = 2;
    q._mem_limit = 64;
    q._scope = "42";   // 组内题
    ASSERT_TRUE(m.AddQuestion(&q));
    EXPECT_EQ(q._id, "2");
    EXPECT_TRUE(fs::exists("./questions/2/desc.txt"));

    std::string list = ReadList();
    EXPECT_EQ(list, "1 判断回文数 简单 1 30 global\n2 新增题 中等 2 64 42\n");

    std::vector<oj_filemodel::Question> out;
    m.GetAllQuestions(&out);
    ASSERT_EQ(out.size(), 2u);
}

// UpdateQuestion: 覆盖四文件并重建列表
TEST_F(FileModelTest, UpdateQuestionOverwritesFilesAndRebuildsList)
{
    SeedList({"1 判断回文数 简单 1 30 global", "2 求最大值 中等 2 64 global"});
    SeedQuestionFiles("1");
    SeedQuestionFiles("2");

    oj_filemodel::Model m;
    oj_filemodel::Question q;
    m.GetOneQuestion("2", &q);
    q._title = "求最大值改";
    q._desc = "updated_desc";
    q._scope = "global";
    ASSERT_TRUE(m.UpdateQuestion(q));

    // 文件被覆盖
    std::string desc;
    {
        std::ifstream in("./questions/2/desc.txt");
        ASSERT_TRUE(in.is_open());
        std::getline(in, desc);
    }
    EXPECT_EQ(desc, "updated_desc");

    // 列表被重建且按数字 id 升序
    EXPECT_EQ(ReadList(), "1 判断回文数 简单 1 30 global\n2 求最大值改 中等 2 64 global\n");
}

// DeleteQuestion: 删除文件与目录并重建列表
TEST_F(FileModelTest, DeleteQuestionRemovesFilesAndDir)
{
    SeedList({"1 判断回文数 简单 1 30 global", "2 求最大值 中等 2 64 global"});
    SeedQuestionFiles("1");
    SeedQuestionFiles("2");

    oj_filemodel::Model m;
    ASSERT_TRUE(m.DeleteQuestion("1"));
    EXPECT_FALSE(fs::exists("./questions/1"));
    EXPECT_TRUE(fs::exists("./questions/2"));
    EXPECT_EQ(ReadList(), "2 求最大值 中等 2 64 global\n");

    std::vector<oj_filemodel::Question> out;
    m.GetAllQuestions(&out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]._id, "2");
}

// WriteQuestionsList 按数字 id 升序重写(而非字典序)
TEST_F(FileModelTest, WriteQuestionsListSortsNumerically)
{
    SeedList({"10 第十题 简单 1 30 global", "2 第二题 简单 1 30 global"});
    SeedQuestionFiles("10");
    SeedQuestionFiles("2");

    oj_filemodel::Model m;
    oj_filemodel::Question q;
    m.GetOneQuestion("2", &q);
    q._title = "第二题改";
    ASSERT_TRUE(m.UpdateQuestion(q));   // 触发 WriteQuestionsList

    EXPECT_EQ(ReadList(), "2 第二题改 简单 1 30 global\n10 第十题 简单 1 30 global\n");
}

// 删除不存在的题目应返回失败
TEST_F(FileModelTest, DeleteMissingQuestionFails)
{
    SeedList({"1 判断回文数 简单 1 30 global"});
    SeedQuestionFiles("1");
    oj_filemodel::Model m;
    EXPECT_FALSE(m.DeleteQuestion("999"));
}
