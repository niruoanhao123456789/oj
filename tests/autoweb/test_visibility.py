"""角色与可见性用例：管理页权限门控 + 组内题的可见性生命周期（SPEC §4.5/§5.x）。"""

from ojui import (
    require, require_in, require_not_in, SkipTest,
    admin_token, unique_name, list_questions, create_question, delete_question,
)


def _register(ui, role, invite_code=None):
    name = unique_name("lead" if role == "leader" else "usr")
    pw = "pw_" + ("lead123" if role == "leader" else "usr123")
    reg = ui.register(name, pw, role, invite_code=invite_code).json or {}
    require(reg.get("ok"), "注册(%s)失败: %s" % (role, reg.get("message")))
    return name, pw


def test_manage_pages_role_gate(ui):
    at = admin_token(ui)
    if not at:
        raise SkipTest("admin 账号不可用")

    # 管理员可见管理页与新增/编辑表单
    manage = ui.get("/question_manage", token=at)
    require(manage.status == 200, "管理列表 HTTP %d" % manage.status)
    require_not_in('id="oj-msg-page"', manage.text, "管理员不应看到引导门")
    require_in("题目管理", manage.text, "管理列表标题")

    edit = ui.get("/question_manage/edit", token=at)
    require_in('id="qform"', edit.text, "题目编辑表单")
    require_in('id="modeFn"', edit.text, "函数接口模式单选")
    require_in('id="modeAc"', edit.text, "ACM 模式单选")
    require_in('id="sampleList"', edit.text, "显式样例列表容器")
    require_in('id="hiddenList"', edit.text, "隐藏测试案例列表容器")
    require_in('id="tailBox"', edit.text, "tail 高级代码区")

    # 普通用户被权限门控
    username, pw = _register(ui, "user")
    utok = ui.login_ok(username, pw)
    denied = ui.get("/question_manage", token=utok)
    require_in("需要管理员或负责人权限", denied.text, "普通用户访问题目管理应被拒绝")
    denied_edit = ui.get("/question_manage/edit", token=utok)
    require_in("需要管理员或负责人权限", denied_edit.text, "普通用户访问新增题目页应被拒绝")


def test_group_question_visibility_lifecycle(ui):
    at = admin_token(ui)
    if not at:
        raise SkipTest("admin 账号不可用")

    # 管理员签发邀请码 -> 注册一名负责人 L
    inv = ui.reset_admin_invite(at).json or {}
    require(inv.get("ok") and inv.get("invite_code"), "重置管理员邀请码失败")
    lname, lpw = _register(ui, "leader", invite_code=inv["invite_code"])
    ltok = ui.login_ok(lname, lpw)

    # L 创建小组
    gname = "UI 小组_" + unique_name("g")
    g = ui.post("/api/groups", {"name": gname}, token=ltok).json or {}
    require(g.get("ok") and g.get("group_id") and g.get("invite_code"),
            "负责人建组失败: %s" % g.get("message"))
    gid = str(g["group_id"])

    # 注册成员 U 与非成员 W
    uname, upw = _register(ui, "user")
    utok = ui.login_ok(uname, upw)
    wname, wpw = _register(ui, "user")
    wtok = ui.login_ok(wname, wpw)

    # U 凭小组邀请码加入
    join = ui.post("/api/groups/join", {"invite_code": g["invite_code"]}, token=utok).json or {}
    require(join.get("ok"), "普通用户加入小组失败: %s" % join.get("message"))

    # L 在组内发布一道 ACM 题
    title = "UI 组内题_" + unique_name("q")
    code = ("#include <iostream>\nusing namespace std;\n"
            "int main(){ int a,b; cin>>a>>b; cout<<a+b<<endl; return 0; }")
    payload = {
        "title": title, "rank": "简单", "scope": gid,
        "desc": "组内题：读入 a b 输出 a+b。",
        "header": "", "answer": code, "tail": "",
        "mode": "acm", "cpu_limit": 1, "mem_limit": 30,
        "visible_cases": [{"name": "示例1", "input": "1 2", "expected": "3"}],
        "hidden_cases": [{"input": "1 2", "expected": "3"}, {"input": "3 4", "expected": "7"}],
    }
    qid = create_question(ui, ltok, payload)

    def _titles(tok):
        return [q["title"] for q in list_questions(ui, tok)]

    try:
        # 发布者(L)与成员(U)可见；非成员(W)不可见
        require(title in _titles(ltok), "负责人应能看到自己的组内题")
        require(title in _titles(at), "管理员应能看到组内题")
        require(title in _titles(utok), "已加入小组的成员应能看到组内题")
        require(title not in _titles(wtok), "非小组成员不应看到组内题")

        # 成员可打开答题页且不泄漏隐藏项
        page = ui.get("/question/%s" % qid, token=utok)
        require(page.status == 200, "成员打开组内题 HTTP %d" % page.status)
        require_in('id="preCode"', page.text, "组内题应有 answer 预填")
        require_not_in("hidden_cases", page.text, "组内题不得泄漏 hidden_cases")

        # 非成员打开组内题被拒绝（服务端返回不可访问提示）
        denied = ui.get("/question/%s" % qid, token=wtok)
        require_in("无法访问", denied.text, "非成员打开组内题应被拒绝")
    finally:
        # 清理：负责人删除组内题，再删除小组（级联清理成员关系）
        try:
            delete_question(ui, ltok, qid)
        except Exception:
            pass
        try:
            ui.delete("/api/groups/%s" % gid, token=ltok)
        except Exception:
            try:
                ui.delete("/api/groups/%s" % gid, token=at)
            except Exception:
                pass
