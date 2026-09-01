// ============================================================================
// tests/example/mysql_test.cpp
// MySQL 模式示例测试(模型层, 直连 MySQL, 无需启动 oj_server / compile_server)
//
//   ./mysql_test            # 重置 + 播种 + 测试(默认)
//   ./mysql_test --reset    # 仅重置用户数据与题目
//   ./mysql_test --seed     # 重置并播种(设计基本用户与题目)
//   ./mysql_test --test     # 仅运行测试(需先播种)
//
// 重置范围: users / groups / group_members / admin_invite / questions(全部清空)
// 播种内容: 6 个基本用户(admin/leader1/leader2/user1/user2/guest)、
//           3 个小组、5 道题(含原「判断回文数」「求最大值」两道全局题)。
// 测试案例: 注册/密码、登录/会话、管理员邀请码、小组与成员、角色管理、
//           题目 CRUD(MySQL)、可见性规则(SPEC.md §4.5)。
// ============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include "../../oj_server/user/oj_user_model.hpp"
#include "../../oj_server/oj_mysqlmodel.hpp"
#include "../../common/log/Log.hpp"

using namespace LogModule;
using oj_user_model::UserModel;
using oj_user_model::User;
using oj_user_model::Group;
using oj_mysqlmodel::Model;
using oj_mysqlmodel::Question;

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

// 播种步骤失败则终止播种
#define SEED_CHECK(cond, name) \
    do { \
        if(!(cond)) { ++g_fail; std::cout << "[FAIL] 播种失败: " << name << std::endl; return false; } \
        std::cout << "[SEED] " << name << std::endl; \
    } while(0)

// ---------------------------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------------------------
static bool ProbeDb()
{
    Model m;
    return m.ExecuteSql("select 1");
}

// 在用户 owner 的小组中按名字查找
static bool FindGroup(UserModel& um, int owner, const std::string& name, Group* out)
{
    std::vector<Group> groups;
    if(!um.GetGroupsByOwner(owner, &groups))
        return false;
    for(const auto& g : groups)
        if(g._name == name)
        {
            *out = g;
            return true;
        }
    return false;
}

// 复刻 Control::CanAccessQuestion 的可见性逻辑(SPEC.md §4.5):
// 全局题所有人可见; admin 可见全部; leader 仅见本组; user 仅见所在组
static bool CanSee(const User* u, const Question& q, UserModel& um)
{
    if(q._scope == "global")
        return true;
    if(u == nullptr)
        return false;
    if(u->_role == "admin")
        return true;
    int gid = std::atoi(q._scope.c_str());
    if(gid <= 0)
        return false;
    if(u->_role == "leader")
    {
        Group g;
        return um.GetGroupById(gid, &g) && g._owner_id == u->_id;
    }
    return um.IsMember(u->_id, gid);
}

// ---------------------------------------------------------------------------
// Phase 1: 重置用户数据与题目
// ---------------------------------------------------------------------------
static bool DoReset()
{
    Model m;
    const char* tables[] = {
        "group_members", "`groups`", "users", "admin_invite", "questions"
    };
    for(const char* t : tables)
    {
        if(!m.ExecuteSql(std::string("TRUNCATE TABLE ") + t))
        {
            std::cout << "[ERR] 重置失败: TRUNCATE TABLE " << t << std::endl;
            return false;
        }
    }
    std::cout << "== 已重置用户数据与题目: users/groups/group_members/admin_invite/questions 已清空" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Phase 2: 设计基本用户与题目
// ---------------------------------------------------------------------------
static bool DoSeed()
{
    UserModel um;
    Model qm;
    std::string role;

    // ---- 用户 ----
    SEED_CHECK(um.Register("admin", "admin123", "user", &role) && role == "admin",
               "注册 admin(首个用户自动成为管理员)");

    std::string invite;
    SEED_CHECK(um.ResetAdminInvite(&invite), "重置管理员邀请码");
    SEED_CHECK(um.IsValidAdminInviteCode(invite), "校验新管理员邀请码有效");

    SEED_CHECK(um.Register("leader1", "leader123", "leader", &role) && role == "leader",
               "注册负责人 leader1(凭管理员邀请码)");
    SEED_CHECK(um.Register("leader2", "leader222", "leader", &role) && role == "leader",
               "注册负责人 leader2(凭管理员邀请码)");

    SEED_CHECK(um.Register("user1", "user123", "user", &role) && role == "user",
               "注册普通用户 user1");
    SEED_CHECK(um.Register("user2", "user223", "user", &role) && role == "user",
               "注册普通用户 user2");
    SEED_CHECK(um.Register("guest", "guest123", "user", &role) && role == "user",
               "注册普通用户 guest");

    User admin, leader1, leader2, user1, user2, guest;
    SEED_CHECK(um.GetUserByName("admin", &admin), "读取用户 admin");
    SEED_CHECK(um.GetUserByName("leader1", &leader1), "读取用户 leader1");
    SEED_CHECK(um.GetUserByName("leader2", &leader2), "读取用户 leader2");
    SEED_CHECK(um.GetUserByName("user1", &user1), "读取用户 user1");
    SEED_CHECK(um.GetUserByName("user2", &user2), "读取用户 user2");
    SEED_CHECK(um.GetUserByName("guest", &guest), "读取用户 guest");

    // ---- 小组(负责人可创建多个) ----
    Group g1, g2, g3;
    SEED_CHECK(um.CreateGroup("算法组", leader1._id, &g1), "leader1 创建小组「算法组」");
    SEED_CHECK(um.CreateGroup("数据结构组", leader2._id, &g2), "leader2 创建小组「数据结构组」");
    SEED_CHECK(um.CreateGroup("第二小组", leader1._id, &g3), "leader1 再创建小组「第二小组」(多个小组)");

    // ---- 成员关系 ----
    SEED_CHECK(um.JoinGroup(user1._id, g1._id), "user1 加入「算法组」");
    SEED_CHECK(um.JoinGroup(user2._id, g1._id), "user2 加入「算法组」");
    SEED_CHECK(um.JoinGroup(user2._id, g2._id), "user2 加入「数据结构组」");

    // ---- 题目(原题 1/2 内容原样保留, 另造 3 道覆盖 global 与组内题) ----
    Question q;

    // 原题 1: 判断回文数(global)
    q = Question();
    q._title = "判断回文数";
    q._rank = Question::EASY;
    q._desc = R"Q1D(判断一个整数是否是回文数。回文数是指正序（从左向右）和倒序（从右向左）读都是一样的整数。

示例 1:

输入: 121
输出: true


示例 2:

输入: -121
输出: false
解释: 从左向右读, 为 -121 。 从右向左读, 为 121- 。因此它不是一个回文数。


示例 3:

输入: 10
输出: false
解释: 从右向左读, 为 01 。因此它不是一个回文数。
进阶:

你能不将整数转为字符串来解决这个问题吗？)Q1D";
    q._header = R"Q1H(#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace std;
)Q1H";
    q._answer = R"Q1A(class Solution
{
public:
    bool isPalindrome(int x)
    {
        // code write here...
    }
};
)Q1A";
    q._tail = R"Q1T(#ifndef COMPILER_ONLINE
#include "header.cpp"
#include "answer.cpp"
#endif

int g_pass = 0, g_total = 0;
#define RUN_TEST(name, cond) do { ++g_total; if(cond) ++g_pass; } while(0)

void test1()
{
    RUN_TEST("回文数121", Solution().isPalindrome(121));
}

void test2()
{
    RUN_TEST("回文数-121", !Solution().isPalindrome(-121));
}

void test3()
{
    RUN_TEST("回文数10", !Solution().isPalindrome(10));
}

int main()
{
    test1();
    test2();
    test3();

    std::cout << "PASSRATE " << g_pass << "/" << g_total << std::endl;
    return 0;
}
)Q1T";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = "global";
    SEED_CHECK(qm.AddQuestion(&q), "入库原题1 判断回文数(global)");

    // 原题 2: 求最大值(global)
    q = Question();
    q._title = "求最大值";
    q._rank = Question::EASY;
    q._desc = R"Q2D(求一个数组中最大值。

示例 1:

输入: {1,2,3,4,5,6,12,3,4,-1}
输出: 12)Q2D";
    q._header = R"Q2H(#include <iostream>
#include <vector>
#include <algorithm>
#include <ctype.h>

using namespace std;
)Q2H";
    q._answer = R"Q2A(class Solution
{
public:
    int Max(const vector<int> &nums)
    {
        // code write here...
    }
};
)Q2A";
    q._tail = R"Q2T(#ifndef COMPILER_ONLINE
#include "header.cpp"
#include "answer.cpp"
#endif

int g_pass = 0, g_total = 0;
#define RUN_TEST(name, cond) do { ++g_total; if(cond) ++g_pass; } while(0)

void test1()
{
    std::vector<int> v = {1,2,3,4,5,6,12,3,4,-1};
    RUN_TEST("最大值12", Solution().Max(v) == 12);
}

int main()
{
    test1();

    std::cout << "PASSRATE " << g_pass << "/" << g_total << std::endl;
    return 0;
}
)Q2T";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = "global";
    SEED_CHECK(qm.AddQuestion(&q), "入库原题2 求最大值(global)");

    // 新题 3: 两数之和(global)
    q = Question();
    q._title = "两数之和";
    q._rank = Question::EASY;
    q._desc = R"Q3D(给定一个整数数组 nums 和一个整数目标值 target, 请你在该数组中找出和为目标值 target 的两个整数, 并返回它们的数组下标。

示例 1:

输入: nums = [2,7,11,15], target = 9
输出: [0,1])Q3D";
    q._header = R"Q3H(#include <iostream>
#include <vector>

using namespace std;
)Q3H";
    q._answer = R"Q3A(class Solution
{
public:
    vector<int> twoSum(vector<int>& nums, int target)
    {
        // code write here...
    }
};
)Q3A";
    q._tail = R"Q3T(#ifndef COMPILER_ONLINE
#include "header.cpp"
#include "answer.cpp"
#endif

int g_pass = 0, g_total = 0;
#define RUN_TEST(name, cond) do { ++g_total; if(cond) ++g_pass; } while(0)

void test1()
{
    vector<int> nums = {2,7,11,15};
    vector<int> ret = Solution().twoSum(nums, 9);
    RUN_TEST("两数之和", ret.size() == 2 && ret[0] == 0 && ret[1] == 1);
}

int main()
{
    test1();

    std::cout << "PASSRATE " << g_pass << "/" << g_total << std::endl;
    return 0;
}
)Q3T";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = "global";
    SEED_CHECK(qm.AddQuestion(&q), "入库新题3 两数之和(global)");

    // 新题 4: 字符串反转(scope = 算法组)
    q = Question();
    q._title = "字符串反转";
    q._rank = Question::NORMAL;
    q._desc = R"Q4D(给定一个字符串 s, 返回它的反转字符串。

示例 1:

输入: "hello"
输出: "olleh")Q4D";
    q._header = R"Q4H(#include <iostream>
#include <string>
#include <algorithm>

using namespace std;
)Q4H";
    q._answer = R"Q4A(class Solution
{
public:
    string reverseString(string s)
    {
        // code write here...
    }
};
)Q4A";
    q._tail = R"Q4T(#ifndef COMPILER_ONLINE
#include "header.cpp"
#include "answer.cpp"
#endif

int g_pass = 0, g_total = 0;
#define RUN_TEST(name, cond) do { ++g_total; if(cond) ++g_pass; } while(0)

void test1()
{
    RUN_TEST("字符串反转", Solution().reverseString("hello") == "olleh");
}

int main()
{
    test1();

    std::cout << "PASSRATE " << g_pass << "/" << g_total << std::endl;
    return 0;
}
)Q4T";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = std::to_string(g1._id);
    SEED_CHECK(qm.AddQuestion(&q), ("入库组内题4 字符串反转(scope=小组" + std::to_string(g1._id) + ")").c_str());

    // 新题 5: 链表反转(scope = 数据结构组)
    q = Question();
    q._title = "链表反转";
    q._rank = Question::DIFFICULT;
    q._desc = R"Q5D(给定一个单链表的头节点 head, 请反转该链表, 并返回反转后的链表头节点。

示例 1:

输入: 1 -> 2 -> 3 -> 4 -> 5
输出: 5 -> 4 -> 3 -> 2 -> 1)Q5D";
    q._header = R"Q5H(#include <iostream>

struct ListNode
{
    int val;
    ListNode* next;
    ListNode(int x) : val(x), next(nullptr) {}
};

using namespace std;
)Q5H";
    q._answer = R"Q5A(class Solution
{
public:
    ListNode* reverseList(ListNode* head)
    {
        // code write here...
    }
};
)Q5A";
    q._tail = R"Q5T(#ifndef COMPILER_ONLINE
#include "header.cpp"
#include "answer.cpp"
#endif

int g_pass = 0, g_total = 0;
#define RUN_TEST(name, cond) do { ++g_total; if(cond) ++g_pass; } while(0)

void test1()
{
    ListNode n1(1), n2(2), n3(3);
    n1.next = &n2; n2.next = &n3;
    ListNode* ret = Solution().reverseList(&n1);
    RUN_TEST("链表反转", ret && ret->val == 3);
}

int main()
{
    test1();

    std::cout << "PASSRATE " << g_pass << "/" << g_total << std::endl;
    return 0;
}
)Q5T";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = std::to_string(g2._id);
    SEED_CHECK(qm.AddQuestion(&q), ("入库组内题5 链表反转(scope=小组" + std::to_string(g2._id) + ")").c_str());

    return true;
}

// ---------------------------------------------------------------------------
// Phase 3: 测试案例
// ---------------------------------------------------------------------------
static void RunTests()
{
    UserModel um;
    Model qm;

    std::cout << "\n----- 注册与密码 -----\n";
    {
        std::string role;
        CHECK(!um.Register("admin", "another", "user", &role), "重复用户名注册失败");
        CHECK(!um.IsValidAdminInviteCode(""), "空管理员邀请码无效");
        CHECK(!um.IsValidAdminInviteCode("wrong-code"), "错误管理员邀请码无效");

        User stored;
        um.GetUserByName("admin", &stored);
        CHECK(!stored._password_hash.empty() && stored._password_hash != "admin123",
              "密码不以明文落库");
        CHECK(stored._password_hash == UserModel::HashPassword("admin123", stored._salt),
              "哈希值与 密码+盐 重算一致");

        std::string h1 = UserModel::HashPassword("abc", "s");
        CHECK(h1 == UserModel::HashPassword("abc", "s"), "同盐同密码哈希确定");
        CHECK(h1 != UserModel::HashPassword("abc", "s2"), "盐变化哈希改变");
        CHECK(h1 != UserModel::HashPassword("abd", "s"), "密码变化哈希改变");
    }

    std::cout << "\n----- 登录与会话 -----\n";
    {
        User out;
        CHECK(um.Login("user1", "user123", &out) && out._role == "user", "正确密码登录成功且角色正确");
        CHECK(um.Login("admin", "admin123", &out) && out._role == "admin", "admin 登录成功");
        CHECK(!um.Login("user1", "wrong-password", &out), "错误密码登录失败");
        CHECK(!um.Login("nobody", "x", &out), "不存在的用户登录失败");

        User u1;
        um.GetUserByName("user1", &u1);
        std::string t1 = um.CreateSession(u1);
        std::string t2 = um.CreateSession(u1);
        CHECK(!t1.empty() && t1 != t2, "会话 token 唯一且非空");
        User from;
        CHECK(um.GetSession(t1, &from) && from._id == u1._id && from._username == "user1",
              "GetSession 可取回会话用户");
        CHECK(!um.GetSession("no-such-token", &from), "未知 token 取回失败");
    }

    std::cout << "\n----- 管理员邀请码 -----\n";
    {
        std::string c1, c2;
        CHECK(um.ResetAdminInvite(&c1) && !c1.empty(), "管理员重置邀请码返回新码");
        CHECK(um.IsValidAdminInviteCode(c1), "新码有效");
        CHECK(um.ResetAdminInvite(&c2) && c2 != c1, "再次重置生成不同的新码");
        CHECK(!um.IsValidAdminInviteCode(c1), "旧码立即失效");
        CHECK(um.IsValidAdminInviteCode(c2), "最新码有效");
    }

    std::cout << "\n----- 小组与成员 -----\n";
    {
        User admin, leader1, leader2, user1, user2, guest;
        um.GetUserByName("admin", &admin);
        um.GetUserByName("leader1", &leader1);
        um.GetUserByName("leader2", &leader2);
        um.GetUserByName("user1", &user1);
        um.GetUserByName("user2", &user2);
        um.GetUserByName("guest", &guest);

        Group g1, g2;
        CHECK(FindGroup(um, leader1._id, "算法组", &g1), "找到 leader1 的小组「算法组」");
        CHECK(FindGroup(um, leader2._id, "数据结构组", &g2), "找到 leader2 的小组「数据结构组」");

        std::vector<Group> l1g, l2g;
        um.GetGroupsByOwner(leader1._id, &l1g);
        um.GetGroupsByOwner(leader2._id, &l2g);
        CHECK(l1g.size() == 2, "leader1 可创建多个小组(2 个)");
        CHECK(l2g.size() == 1, "leader2 拥有 1 个小组");

        std::vector<int> u1g, u2g, gg;
        um.GetUserGroups(user1._id, &u1g);
        um.GetUserGroups(user2._id, &u2g);
        um.GetUserGroups(guest._id, &gg);
        CHECK(u1g.size() == 1 && u1g[0] == g1._id, "user1 属于「算法组」");
        CHECK(u2g.size() == 2, "user2 属于两个小组");
        CHECK(gg.empty(), "guest 未加入任何小组");

        CHECK(um.IsMember(user1._id, g1._id), "IsMember(user1, 算法组) == true");
        CHECK(!um.IsMember(guest._id, g1._id), "IsMember(guest, 算法组) == false");

        Group bogus;
        CHECK(!um.GetGroupByInvite("bad-invite-code", &bogus), "错误邀请码查不到小组");

        std::string old_code = g1._invite_code, new_code;
        CHECK(um.ResetInviteCode(g1._id, &new_code) && new_code != old_code, "重置小组邀请码");
        Group by_old, by_new;
        CHECK(!um.GetGroupByInvite(old_code, &by_old), "旧小组邀请码失效");
        CHECK(um.GetGroupByInvite(new_code, &by_new) && by_new._id == g1._id, "新小组邀请码有效");

        CHECK(!um.JoinGroup(user1._id, g1._id), "重复加入同一小组失败");
    }

    std::cout << "\n----- 角色管理 -----\n";
    {
        User user1;
        um.GetUserByName("user1", &user1);
        CHECK(um.UpdateRole(user1._id, "leader"), "管理员将 user1 升级为 leader");
        User back;
        um.GetUserById(user1._id, &back);
        CHECK(back._role == "leader", "升级后角色为 leader");
        CHECK(um.UpdateRole(user1._id, "user"), "将 user1 降回 user");
        um.GetUserById(user1._id, &back);
        CHECK(back._role == "user", "降级后角色为 user");
    }

    std::cout << "\n----- 题目管理(MySQL 模式) -----\n";
    {
        Group g1;
        {
            User leader1;
            um.GetUserByName("leader1", &leader1);
            FindGroup(um, leader1._id, "算法组", &g1);
        }

        Question nq;
        nq._title = "测试新增题";
        nq._rank = Question::DIFFICULT;
        nq._desc = "用于 CRUD 测试的题目";
        nq._header = "// header";
        nq._answer = "// answer";
        nq._tail = "// tail";
        nq._cpu_limit = 2;
        nq._mem_limit = 64;
        nq._scope = "global";
        CHECK(qm.AddQuestion(&nq) && !nq._id.empty(), "AddQuestion 新增题目并回填 id");

        Question back;
        qm.GetOneQuestion(nq._id, &back);
        CHECK(back._id == nq._id && back._title == nq._title &&
              back._rank == Question::DIFFICULT && back._scope == "global",
              "GetOneQuestion 字段往返一致");

        nq._title = "测试新增题_改";
        nq._scope = std::to_string(g1._id);
        CHECK(qm.UpdateQuestion(nq), "UpdateQuestion 修改题目");
        qm.GetOneQuestion(nq._id, &back);
        CHECK(back._title == "测试新增题_改" && back._scope == std::to_string(g1._id),
              "修改后的标题与 scope 生效");

        CHECK(qm.DeleteQuestion(nq._id), "DeleteQuestion 删除题目");
        Question gone;
        qm.GetOneQuestion(nq._id, &gone);
        CHECK(gone._id.empty(), "删除后题目不可再查到");

        Question none;
        qm.GetOneQuestion("99999", &none);
        CHECK(none._id.empty(), "查询不存在的题目返回空");
    }

    std::cout << "\n----- 题目数据与可见性 -----\n";
    {
        std::vector<Question> qs;
        qm.GetAllQuestions(&qs);
        CHECK(qs.size() == 5, "播种后题库共 5 道题");

        Question q1, q2;
        qm.GetOneQuestion("1", &q1);
        qm.GetOneQuestion("2", &q2);
        CHECK(q1._title == "判断回文数" && q1._scope == "global", "原题1 判断回文数 恢复(global)");
        CHECK(q2._title == "求最大值" && q2._scope == "global", "原题2 求最大值 恢复(global)");
        CHECK(!q1._header.empty() && !q1._answer.empty() && !q1._tail.empty() && !q1._desc.empty(),
              "原题1 内容完整(header/answer/tail/desc)");
        CHECK(!q2._header.empty() && !q2._answer.empty() && !q2._tail.empty() && !q2._desc.empty(),
              "原题2 内容完整(header/answer/tail/desc)");

        int global_count = 0, g1_count = 0, g2_count = 0;
        Group g1, g2;
        {
            User leader1, leader2;
            um.GetUserByName("leader1", &leader1);
            um.GetUserByName("leader2", &leader2);
            FindGroup(um, leader1._id, "算法组", &g1);
            FindGroup(um, leader2._id, "数据结构组", &g2);
        }
        for(const auto& q : qs)
        {
            if(q._scope == "global")
                ++global_count;
            else if(q._scope == std::to_string(g1._id))
                ++g1_count;
            else if(q._scope == std::to_string(g2._id))
                ++g2_count;
        }
        CHECK(global_count == 3, "全局题 3 道");
        CHECK(g1_count == 1 && g2_count == 1, "组内题各 1 道(scope 指向两个小组)");

        User admin, leader1, leader2, user1, user2, guest;
        um.GetUserByName("admin", &admin);
        um.GetUserByName("leader1", &leader1);
        um.GetUserByName("leader2", &leader2);
        um.GetUserByName("user1", &user1);
        um.GetUserByName("user2", &user2);
        um.GetUserByName("guest", &guest);

        auto visible = [&](const User* u) {
            int n = 0;
            for(const auto& q : qs)
                if(CanSee(u, q, um))
                    ++n;
            return n;
        };
        CHECK(visible(&admin) == 5, "admin 可见全部 5 道题");
        CHECK(visible(&leader1) == 4, "leader1 仅见 global+本组(4 道)");
        CHECK(visible(&leader2) == 4, "leader2 仅见 global+本组(4 道)");
        CHECK(visible(&user1) == 4, "user1 见 global+所在组(4 道)");
        CHECK(visible(&user2) == 5, "user2 见 global+两个小组(5 道)");
        CHECK(visible(&guest) == 3, "guest 仅见全局(3 道)");
        CHECK(visible(nullptr) == 3, "匿名仅见全局(3 道)");
    }
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    BuildTestLogger();

    std::string mode = "all";
    if(argc > 1)
        mode = argv[1];

    if(!ProbeDb())
    {
        std::cout << "[ERR] MySQL 不可用(127.0.0.1:3306 / oj / oj_client), 请检查数据库后重试." << std::endl;
        return 1;
    }

    if(mode == "--reset" || mode == "--seed" || mode == "all")
        if(!DoReset())
            return 1;
    if(mode == "--seed" || mode == "all")
        if(!DoSeed())
        {
            std::cout << "播种失败, 终止." << std::endl;
            return 1;
        }
    if(mode == "--test" || mode == "all")
        RunTests();

    std::cout << "\n==== MySQL 模式结果汇总: " << g_pass << " 通过, " << g_fail << " 失败 ====" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
