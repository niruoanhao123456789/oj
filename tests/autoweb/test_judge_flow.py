"""判题流程用例：函数接口模式与 ACM（传统 IO）模式（SPEC §5.3 / §6 / §9）。"""

from ojui import (
    require, SkipTest, admin_token, find_question,
    create_question, delete_question, acm_payload, unique_name,
)

GLOBAL_DEMO = "判断回文数"

PALINDROME_OK = (
    "bool isPalindrome(int x){\n"
    "    if (x < 0) return false;\n"
    "    long long r = 0, t = x;\n"
    "    while (t) { r = r * 10 + t % 10; t /= 10; }\n"
    "    return r == (long long)x;\n"
    "}"
)
PALINDROME_BAD = "bool isPalindrome(int x){ return true; }"
PALINDROME_BROKEN = "bool isPalindrome("

ACM_OK = (
    "#include <iostream>\nusing namespace std;\n"
    "int main(){ long long a,b; cin >> a >> b; cout << a + b << endl; return 0; }"
)
ACM_BAD = (
    "#include <iostream>\nusing namespace std;\n"
    "int main(){ long long a,b; cin >> a >> b; cout << a + b - 1 << endl; return 0; }"
)
ACM_BROKEN = "int main( { return 0; }"


def _judge(ui, token, qid, code):
    r = ui.post("/judge/%s" % qid, {"code": code}, token=token)
    data = r.json or {}
    require(r.status == 200, "/judge HTTP %d" % r.status)
    return data


def test_judge_function_mode_question(ui):
    at = admin_token(ui)
    if not at:
        raise SkipTest("admin 账号不可用")
    q = find_question(ui, at, GLOBAL_DEMO)
    if not q:
        raise SkipTest("演示题「%s」不存在" % GLOBAL_DEMO)

    ok = _judge(ui, at, q["id"], PALINDROME_OK)
    require(ok.get("status") == 0, "正确实现应编译运行成功: %s" % ok)
    require(ok.get("total_count", 0) > 0, "应有隐藏用例被判题")
    require(ok.get("pass_count") == ok.get("total_count"), "正确实现应全部通过")

    bad = _judge(ui, at, q["id"], PALINDROME_BAD)
    require(bad.get("status") == 0 and bad.get("pass_count", 0) < bad.get("total_count", 1),
            "错误实现应部分/未通过: %s" % bad)

    err = _judge(ui, at, q["id"], PALINDROME_BROKEN)
    require(err.get("status") == -3, "语法错误应返回 status=-3(编译失败): %s" % err)
    require("error" in (err.get("reason") or "").lower(), "编译失败 reason 应含错误信息")


def test_judge_acm_mode_question(ui):
    at = admin_token(ui)
    if not at:
        raise SkipTest("admin 账号不可用")

    hidden = [
        {"input": "1 2", "expected": "3"},
        {"input": "-5 8", "expected": "3"},
        {"input": "10 20", "expected": "30"},
    ]
    payload = acm_payload("UI 求和_" + unique_name("acm"), ACM_OK, hidden,
                          visible_cases=[{"name": "示例1", "input": "1 2", "expected": "3"}])
    qid = create_question(ui, at, payload)
    try:
        ok = _judge(ui, at, qid, ACM_OK)
        require(ok.get("status") == 0, "ACM 正确程序应通过: %s" % ok)
        require(ok.get("pass_count") == 3 and ok.get("total_count") == 3,
                "ACM 正确程序应 3/3，实际 %s" % ok)

        bad = _judge(ui, at, qid, ACM_BAD)
        require(bad.get("status") == 0 and bad.get("pass_count") == 0,
                "ACM 错误程序应 0/3，实际 %s" % bad)

        err = _judge(ui, at, qid, ACM_BROKEN)
        require(err.get("status") == -3, "ACM 语法错误应编译失败: %s" % err)
    finally:
        delete_question(ui, at, qid)
