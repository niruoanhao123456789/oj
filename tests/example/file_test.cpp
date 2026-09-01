// ============================================================================
// tests/example/file_test.cpp
// file 模式示例测试(文件模型, 不依赖 MySQL)
// 在沙箱目录 tests/example/filemodel/questions/ 中重置并设计测试题,
// 验证 oj_filemodel(Model) 对 questions.list(6 列, 兼容旧 5 列)的解析,
// 以及 Add/Update/Delete 对四文件与列表的读写。
//
//   ./file_test            # 重置沙箱 + 设计测试题 + 测试(默认)
//   ./file_test --reset    # 仅重置沙箱
//   ./file_test --seed     # 重置并设计测试题
//   ./file_test --test     # 仅运行测试(需先设计)
//
// 说明: 文件模型路径硬编码为 ./questions/(相对工作目录), 因此运行测试时
//       会临时 chdir 到沙箱目录, 结束后切回原目录, 不触碰 oj_server/questions/。
// ============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <limits.h>
#include "../../oj_server/oj_filemodel.hpp"
#include "../../common/log/Log.hpp"

using namespace LogModule;
using oj_filemodel::Model;
using oj_filemodel::Question;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// 日志器(与网关同名, 输出到标准输出; 模型内部 LOG_* 依赖它)
// ---------------------------------------------------------------------------
void BuildTestLogger()
{
    static bool built = false;
    if(built)
        return;
    std::unique_ptr<LoggerBuilder> builder = std::make_unique<GobalLoggerBuilder>();
    builder->BuildLoggerName("oj_Logger");
    builder->BuildLoggerType(LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<StdOutSink>();
    builder->Build();
    built = true;
}

// ---------------------------------------------------------------------------
// 断言与统计
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) \
    do { \
        if(cond) { ++g_pass; std::cout << "[PASS] " << name << std::endl; } \
        else     { ++g_fail; std::cout << "[FAIL] " << name << std::endl; } \
    } while(0)

// ---------------------------------------------------------------------------
// 沙箱路径辅助(以可执行文件所在目录为基准, 与运行时的 cwd 无关)
// ---------------------------------------------------------------------------
static std::string g_sandbox;

static std::string ExeDir()
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if(n <= 0)
        return ".";
    buf[n] = '\0';
    std::string p(buf);
    size_t pos = p.find_last_of('/');
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

static void WriteFile(const std::string& path, const std::string& content)
{
    std::ofstream ofs(path);
    ofs << content;
}

static std::vector<std::string> ReadLines(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream ifs(path);
    std::string line;
    while(std::getline(ifs, line))
        lines.push_back(line);
    return lines;
}

// ---------------------------------------------------------------------------
// Phase 1: 重置沙箱
// ---------------------------------------------------------------------------
static bool DoReset()
{
    fs::remove_all(g_sandbox);
    fs::create_directories(g_sandbox + "/questions");
    std::cout << "== 已重置 file 模式沙箱: " << g_sandbox << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Phase 2: 设计测试题(写 questions.list + 每题四文件)
//   questions.list: 前 2 行为现有题目(6 列), 第 3 行为组内题(6 列, scope=7),
//                   第 4 行为旧 5 列格式(缺省 scope=global, 兼容性测试)
// ---------------------------------------------------------------------------
static bool DoSeed()
{
    std::string base = g_sandbox + "/questions";

    // 题 1: 判断回文数(global)
    fs::create_directories(base + "/1");
    WriteFile(base + "/1/desc.txt",   "判断一个整数是否是回文数。");
    WriteFile(base + "/1/header.cpp", "#include <iostream>\n#include <string>\nusing namespace std;\n");
    WriteFile(base + "/1/answer.cpp", "class Solution {\npublic:\n    bool isPalindrome(int x){ return true; }\n};\n");
    WriteFile(base + "/1/tail.cpp",   "#ifndef COMPILER_ONLINE\n#include \"header.cpp\"\n#include \"answer.cpp\"\n#endif\nint main(){ return 0; }\n");

    // 题 2: 求最大值(global)
    fs::create_directories(base + "/2");
    WriteFile(base + "/2/desc.txt",   "求一个数组中最大值。");
    WriteFile(base + "/2/header.cpp", "#include <iostream>\n#include <vector>\nusing namespace std;\n");
    WriteFile(base + "/2/answer.cpp", "class Solution {\npublic:\n    int Max(const vector<int>& v){ return 0; }\n};\n");
    WriteFile(base + "/2/tail.cpp",   "#ifndef COMPILER_ONLINE\n#include \"header.cpp\"\n#include \"answer.cpp\"\n#endif\nint main(){ return 0; }\n");

    // 题 3: 字符串反转(组内题, scope=7)
    fs::create_directories(base + "/3");
    WriteFile(base + "/3/desc.txt",   "给定一个字符串, 返回反转后的字符串。");
    WriteFile(base + "/3/header.cpp", "#include <iostream>\n#include <string>\nusing namespace std;\n");
    WriteFile(base + "/3/answer.cpp", "class Solution {\npublic:\n    string reverseString(string s){ return s; }\n};\n");
    WriteFile(base + "/3/tail.cpp",   "#ifndef COMPILER_ONLINE\n#include \"header.cpp\"\n#include \"answer.cpp\"\n#endif\nint main(){ return 0; }\n");

    // 题 4: 旧 5 列格式兼容题(缺省 scope=global)
    fs::create_directories(base + "/4");
    WriteFile(base + "/4/desc.txt",   "旧 5 列格式兼容测试题。");
    WriteFile(base + "/4/header.cpp", "#include <iostream>\nusing namespace std;\n");
    WriteFile(base + "/4/answer.cpp", "int main(){ return 0; }\n");
    WriteFile(base + "/4/tail.cpp",   "// no tests\n");

    WriteFile(base + "/questions.list",
        "1 判断回文数 简单 1 30 global\n"
        "2 求最大值 简单 1 30 global\n"
        "3 字符串反转 中等 1 30 7\n"
        "4 旧格式兼容题 简单 1 30\n");

    std::cout << "== 已设计 file 模式测试题: 2 道全局题 + 1 道组内题 + 1 道旧格式题" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Phase 3: 测试案例(在沙箱目录内运行)
// ---------------------------------------------------------------------------
static void RunTests()
{
    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof(cwd)) == nullptr)
        strcpy(cwd, ".");
    if(chdir(g_sandbox.c_str()) != 0)
    {
        ++g_fail;
        std::cout << "[FAIL] 无法进入沙箱目录: " << g_sandbox << std::endl;
        return;
    }

    Model m;  // 构造时加载 ./questions/questions.list

    std::cout << "\n----- 列表解析与读取 -----\n";
    {
        std::vector<Question> all;
        m.GetAllQuestions(&all);
        CHECK(all.size() == 4, "questions.list 共解析 4 道题");

        Question q1, q2, q3, q4, none;
        m.GetOneQuestion("1", &q1);
        m.GetOneQuestion("2", &q2);
        m.GetOneQuestion("3", &q3);
        m.GetOneQuestion("4", &q4);
        m.GetOneQuestion("999", &none);

        CHECK(q1._title == "判断回文数" && q1._rank == Question::EASY &&
              q1._cpu_limit == 1 && q1._mem_limit == 30 && q1._scope == "global",
              "6列解析: 原题1 判断回文数(global)");
        CHECK(q2._title == "求最大值" && q2._scope == "global", "6列解析: 原题2 求最大值(global)");
        CHECK(q3._title == "字符串反转" && q3._rank == Question::NORMAL && q3._scope == "7",
              "6列解析: 组内题 scope=7");
        CHECK(q4._title == "旧格式兼容题" && q4._scope == "global", "5列兼容: 缺省 scope=global");
        CHECK(!q1._desc.empty() && !q1._header.empty() && !q1._answer.empty() && !q1._tail.empty(),
              "每题四文件内容读取完整");
        CHECK(none._id.empty(), "查询不存在的题目返回空");
    }

    std::cout << "\n----- Add / Update / Delete(文件模型) -----\n";
    {
        // AddQuestion: 分配 max(id)+1, 写四文件, 追加列表行
        Question nq;
        nq._title = "文件模型新增题";
        nq._rank = Question::DIFFICULT;
        nq._desc = "新增题的描述";
        nq._header = "h";
        nq._answer = "a";
        nq._tail = "t";
        nq._cpu_limit = 2;
        nq._mem_limit = 64;
        nq._scope = "global";
        CHECK(m.AddQuestion(&nq), "AddQuestion 成功");
        CHECK(nq._id == "5", "AddQuestion 分配 max(id)+1 -> 5");
        CHECK(fs::exists(g_sandbox + "/questions/5/desc.txt") &&
              fs::exists(g_sandbox + "/questions/5/tail.cpp"),
              "AddQuestion 写入四文件");
        {
            auto lines = ReadLines(g_sandbox + "/questions/questions.list");
            CHECK(lines.size() == 5 && lines[4].find("5 文件模型新增题") == 0, "AddQuestion 追加列表行");
        }
        {
            Model fresh;  // 重新加载, 验证磁盘持久化
            Question back;
            fresh.GetOneQuestion("5", &back);
            CHECK(back._title == "文件模型新增题" && back._scope == "global",
                  "重新加载后新增题可查到");
        }

        // UpdateQuestion: 覆盖四文件并重建列表
        nq._title = "文件模型新增题_改";
        nq._scope = "7";
        CHECK(m.UpdateQuestion(nq), "UpdateQuestion 成功");
        {
            Model fresh;
            Question back;
            fresh.GetOneQuestion("5", &back);
            CHECK(back._title == "文件模型新增题_改" && back._scope == "7",
                  "重新加载后修改生效(标题/scope)");
        }

        // DeleteQuestion: 删除文件与目录并重建列表
        CHECK(m.DeleteQuestion("5"), "DeleteQuestion 成功");
        CHECK(!fs::exists(g_sandbox + "/questions/5"), "DeleteQuestion 删除目录与文件");
        {
            Model fresh;
            Question back;
            fresh.GetOneQuestion("5", &back);
            CHECK(back._id.empty(), "重新加载后已删除题不可查");
        }
        {
            auto lines = ReadLines(g_sandbox + "/questions/questions.list");
            CHECK(lines.size() == 4, "DeleteQuestion 重建列表(4 行)");
        }

        // 再次新增, id 仍基于当前最大 id 分配
        Question nq2;
        nq2._title = "第二次新增";
        nq2._rank = Question::EASY;
        nq2._desc = "d";
        nq2._header = "h";
        nq2._answer = "a";
        nq2._tail = "t";
        nq2._cpu_limit = 1;
        nq2._mem_limit = 30;
        nq2._scope = "global";
        CHECK(m.AddQuestion(&nq2) && nq2._id == "5", "再次新增 id 仍为 max+1(5)");
        CHECK(m.DeleteQuestion("5"), "清理测试题");
    }

    chdir(cwd);
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    BuildTestLogger();
    g_sandbox = ExeDir() + "/filemodel";

    std::string mode = "all";
    if(argc > 1)
        mode = argv[1];

    if(mode == "--reset" || mode == "--seed" || mode == "all")
        DoReset();
    if(mode == "--seed" || mode == "all")
        if(!DoSeed())
        {
            std::cout << "设计测试题失败, 终止." << std::endl;
            return 1;
        }
    if(mode == "--test" || mode == "all")
        RunTests();

    std::cout << "\n==== file 模式结果汇总: " << g_pass << " 通过, " << g_fail << " 失败 ====" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
