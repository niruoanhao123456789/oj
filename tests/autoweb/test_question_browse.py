"""题目浏览 / 答题页 Web UI 用例（对应 SPEC §4.1 / §5.1 / §5.2）。"""

from ojui import (
    require, require_in, require_not_in, SkipTest,
    list_questions, extract_text_script, extract_json_script, admin_token,
)

GLOBAL_DEMO = "判断回文数"


def test_admin_browses_list_and_opens_question(ui):
    at = admin_token(ui)
    if not at:
        raise SkipTest("admin 账号不可用，跳过题目浏览用例")

    qs = list_questions(ui, at)
    if not qs:
        raise SkipTest("题库为空（示例数据未播种）")

    target = next((q for q in qs if q["title"] == GLOBAL_DEMO), qs[0])

    # 题目列表页结构
    list_html = ui.get("/all_questions", token=at).text
    require_in("题目列表", list_html, "题目列表页标题")
    require_in('href="/question/%s"' % target["id"], list_html, "列表含题目链接")

    # 答题页：编辑器预填 answer、示例卡、隐藏内容不泄漏
    page = ui.get("/question/%s" % target["id"], token=at)
    require(page.status == 200, "答题页 HTTP %d" % page.status)
    html = page.text
    require_not_in('id="oj-msg-page"', html, "登录态不应是引导门")
    require_in('id="preCode"', html, "answer 预填脚本")
    require_in('id="code"', html, "代码编辑器容器")
    require_in("在线编辑区", html, "编辑器标题")

    pc = extract_text_script(html, "preCode")
    require(pc is not None and pc.strip() != "", "answer 预填代码不能为空")

    # 显式样例(#samplesJson)可解析，且数组元素含 input/expected
    samples = extract_json_script(html, "samplesJson")
    require(samples is not None, "缺少 samplesJson(显式样例)数据")
    if samples:
        for s in samples:
            require("input" in s and "expected" in s, "样例元素必须含 input 与 expected")

    # 隐藏项（header/tail/hidden_cases）绝不出现在答题页
    require_not_in("hidden_cases", html, "答题页不得包含 hidden_cases")
    require_not_in("PASSRATE", pc, "预填的 answer 代码不应含 PASSRATE")
    require_not_in("RUN_TEST", pc, "预填的 answer 代码不应含测试驱动代码")

    if target["title"] == GLOBAL_DEMO:
        require_in("bool isPalindrome", pc, "回文数演示题 answer 应为函数骨架")
        require_not_in("int main", pc, "函数接口模式答题页预填不应含 main(隐藏驱动在 tail)")


def test_regular_user_can_browse_global_questions(ui):
    # 注册一个全新普通用户（无任何小组），验证只能看到全局题并可进入答题
    import time
    username = "ui_brw_%d" % int(time.time())
    reg = ui.register(username, "pw_brw123", "user").json or {}
    require(reg.get("ok"), "浏览用普通用户注册失败")
    tok = ui.login_ok(username, "pw_brw123")

    qs = list_questions(ui, tok)
    if not qs:
        raise SkipTest("普通用户无可见题目（题库为空）")

    page = ui.get("/question/%s" % qs[0]["id"], token=tok)
    require(page.status == 200, "普通用户打开答题页 HTTP %d" % page.status)
    require_in('id="preCode"', page.text, "答题页应有 answer 预填")
    require_not_in("hidden_cases", page.text, "答题页不得泄漏 hidden_cases")
