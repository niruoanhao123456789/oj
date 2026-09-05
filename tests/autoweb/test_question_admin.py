"""题目管理用例：负责人/管理员发布题目（函数接口 + ACM）、编辑页预填、修改与删除
（SPEC §5.13~§5.16、API.md Question Management）。"""

from ojui import (
    require, SkipTest,
    admin_token, list_questions, unique_name,
    create_question, update_question, delete_question,
    extract_json_script,
)

FUNC_HEADER = "#include <iostream>\n#include <string>\nusing namespace std;"
FUNC_ANSWER = "bool isOdd(int x)\n{\n    // 在此编写你的代码\n}"
FUNC_TAIL = (
    "int main()\n{\n"
    "    int x;\n    cin >> x;\n"
    '    cout << (isOdd(x) ? "true" : "false") << endl;\n'
    "    return 0;\n}"
)
FUNC_HIDDEN = [
    {"input": "3", "expected": "true"},
    {"input": "-4", "expected": "false"},
    {"input": "0", "expected": "false"},
]

ACM_CODE = (
    "#include <iostream>\nusing namespace std;\n"
    "int main(){ long long a,b; cin >> a >> b; cout << a + b << endl; return 0; }"
)
ACM_HIDDEN = [
    {"input": "1 2", "expected": "3"},
    {"input": "-5 8", "expected": "3"},
]


def test_admin_function_question_crud_and_edit_prefill(ui):
    at = admin_token(ui)
    if not at:
        raise SkipTest("admin 账号不可用")

    title = "UI 函数题_" + unique_name("fn")
    payload = {
        "title": title, "rank": "简单", "scope": "global",
        "desc": "补全 bool isOdd(int x)：奇数返回 true。输入一个整数，输出 true/false。",
        "header": FUNC_HEADER, "answer": FUNC_ANSWER, "tail": FUNC_TAIL,
        "mode": "function", "cpu_limit": 1, "mem_limit": 30,
        "visible_cases": [{"name": "示例1", "input": "3", "expected": "true"}],
        "hidden_cases": FUNC_HIDDEN,
    }
    qid = create_question(ui, at, payload)
    try:
        # 列表页出现新题
        titles = [q["title"] for q in list_questions(ui, at)]
        require(title in titles, "新建函数题应出现在题目列表")

        # 编辑页预填：qdata 含 mode / answer / 用例
        page = ui.get("/question_manage/edit/%s" % qid, token=at)
        qdata = extract_json_script(page.text, "qdata")
        require(qdata is not None, "编辑页应内嵌 qdata")
        require(qdata.get("mode") == "function", "qdata.mode 应为 function")
        require("bool isOdd" in (qdata.get("answer") or ""), "qdata.answer 应为函数骨架")
        require("int main" in (qdata.get("tail") or ""), "qdata.tail 应为隐藏驱动")
        require(len(qdata.get("hidden_cases") or []) == 3, "qdata.hidden_cases 应完整回填")

        # 判题：正确实现全部通过
        judge = ui.post("/judge/%s" % qid, {"code": "bool isOdd(int x){ return (x % 2 != 0); }"},
                        token=at).json or {}
        require(judge.get("status") == 0, "函数题正确实现应通过: %s" % judge)
        require(judge.get("pass_count") == judge.get("total_count") == 3,
                "函数题应 3/3，实际 %s" % judge)

        # 修改标题
        payload2 = dict(payload)
        payload2["title"] = title + "_改"
        update_question(ui, at, qid, payload2)
        titles2 = [q["title"] for q in list_questions(ui, at)]
        require((title + "_改") in titles2 and title not in titles2, "修改标题应生效")
    finally:
        delete_question(ui, at, qid)
        titles3 = [q["title"] for q in list_questions(ui, at)]
        require((title + "_改") not in titles3 and title not in titles3, "删除后题目应消失")


def test_admin_acm_question_crud(ui):
    at = admin_token(ui)
    if not at:
        raise SkipTest("admin 账号不可用")

    title = "UI ACM 题_" + unique_name("acm")
    payload = {
        "title": title, "rank": "中等", "scope": "global",
        "desc": "读入 a、b，输出 a+b。",
        "header": "", "answer": ACM_CODE, "tail": "",
        "mode": "acm", "cpu_limit": 1, "mem_limit": 30,
        "visible_cases": [{"name": "示例1", "input": "1 2", "expected": "3"}],
        "hidden_cases": ACM_HIDDEN,
    }
    qid = create_question(ui, at, payload)
    try:
        page = ui.get("/question_manage/edit/%s" % qid, token=at)
        qdata = extract_json_script(page.text, "qdata")
        require(qdata is not None and qdata.get("mode") == "acm", "ACM 题 qdata.mode 应为 acm")
        require("int main" in (qdata.get("answer") or ""), "ACM 题 answer 应含 main")

        titles = [q["title"] for q in list_questions(ui, at)]
        require(title in titles, "新建 ACM 题应出现在列表")
    finally:
        delete_question(ui, at, qid)
        titles = [q["title"] for q in list_questions(ui, at)]
        require(title not in titles, "ACM 题删除后应消失")
