"""tests/autoweb 轻量 Web/UI 客户端与断言工具。

说明：网关的 HTML 页面 + 原生 XHR/fetch 的 JSON 接口共同构成“Web UI”。
本模块提供访问页面、调用 JSON 接口、解析服务端渲染内容的工具，供各用例使用。
"""

import json
import re
import urllib.parse

import requests

import config


class SkipTest(Exception):
    """跳过当前用例（例如依赖的演示数据不存在）。"""


def _join(base, path):
    return urllib.parse.urljoin(base, path)


class Resp:
    """统一封装一个 HTTP 响应。"""

    def __init__(self, r):
        self.status = r.status_code
        # 网关静态资源可能未声明 charset，统一按 UTF-8 解码（页面/JSON 均为 UTF-8）
        self.text = r.content.decode("utf-8", "replace")
        self.headers = r.headers

    @property
    def json(self):
        """尽力把响应体解析为 JSON（页面失败时返回 None）。"""
        if not self.text:
            return None
        try:
            return json.loads(self.text)
        except Exception:
            return None

    def __repr__(self):
        return "Resp(status=%s, len=%d)" % (self.status, len(self.text))


class UI:
    """针对 oj_server 的页面 / API 客户端（无浏览器，但复刻 UI 的调用方式）。"""

    def __init__(self, base_url=None, timeout=None):
        self.base = (base_url or config.DEFAULT_BASE_URL).rstrip("/") + "/"
        self.timeout = timeout or config.TIMEOUT
        self.session = requests.Session()

    # ---------- 基础请求 ----------
    def request(self, method, path, body=None, token=None):
        headers = {}
        if token:
            headers["Authorization"] = "Bearer " + token
        kwargs = {"headers": headers, "timeout": self.timeout}
        if body is not None:
            headers.setdefault("Content-Type", "application/json; charset=utf-8")
            kwargs["json"] = body
        r = self.session.request(method, _join(self.base, path), **kwargs)
        return Resp(r)

    def get(self, path, token=None):
        return self.request("GET", path, token=token)

    def post(self, path, body=None, token=None):
        return self.request("POST", path, body=body, token=token)

    def put(self, path, body=None, token=None):
        return self.request("PUT", path, body=body, token=token)

    def delete(self, path, token=None):
        return self.request("DELETE", path, token=token)

    # ---------- 常用账号/流程 ----------
    def login(self, username, password):
        """POST /api/login（登录页表单同款接口）。"""
        return self.post("/api/login", {"username": username, "password": password})

    def login_ok(self, username, password):
        """登录成功则返回 token，否则抛错。"""
        resp = self.login(username, password)
        data = resp.json or {}
        if not data.get("ok") or not data.get("token"):
            raise AssertionError("login failed for %s: %s" % (username, data.get("message")))
        return data["token"]

    def register(self, username, password, role="user", invite_code=None):
        body = {"username": username, "password": password, "role": role}
        if invite_code is not None:
            body["invite_code"] = invite_code
        return self.post("/api/register", body)

    def logout(self, token):
        return self.post("/api/logout", token=token)

    def reset_admin_invite(self, admin_token):
        return self.post("/api/admin/invite", token=admin_token)


# ---------------------------------------------------------------------------
# 页面解析工具
# ---------------------------------------------------------------------------

_QUESTION_LINK = re.compile(r'<a href="/question/(\d+)" data-fetch="1">([^<]+)</a>')


def list_questions(ui, token):
    """GET /all_questions 后解析可见题目列表 [{id,title}, ...]（按页面顺序）。"""
    page = ui.get("/all_questions", token=token)
    if page.status != 200:
        raise AssertionError("all_questions HTTP %d" % page.status)
    if 'id="oj-msg-page"' in page.text:
        raise AssertionError("all_questions 返回登录引导门（token 无效？）")
    out, seen = [], set()
    for qid, text in _QUESTION_LINK.findall(page.text):
        if text.strip() == qid:      # 第一列是“题号”锚点，跳过
            continue
        if qid in seen:
            continue
        seen.add(qid)
        out.append({"id": qid, "title": text.strip()})
    return out


def has_substring(html, needle):
    return needle in html


def extract_json_script(html, element_id):
    """取出 <script type="application/json" id="...">...</script> 的 JSON。"""
    m = re.search(r'<script[^>]*id="%s"[^>]*>(.*?)</script>' % re.escape(element_id), html, re.S)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except Exception:
        raise AssertionError("JSON 脚本 #%s 解析失败" % element_id)


def extract_text_script(html, element_id):
    """取出 <script type="text/plain" id="...">...</script> 的文本（如 answer 预填）。"""
    m = re.search(r'<script[^>]*id="%s"[^>]*>(.*?)</script>' % re.escape(element_id), html, re.S)
    return m.group(1) if m else None


# ---------------------------------------------------------------------------
# 断言与校验
# ---------------------------------------------------------------------------

def require(cond, msg):
    if not cond:
        raise AssertionError(msg)


def require_in(needle, html, what=""):
    if needle not in html:
        raise AssertionError("页面缺少期望内容 %s: %r" % (what, needle))


def require_not_in(needle, html, what=""):
    if needle in html:
        raise AssertionError("页面不应出现 %s: %r" % (what, needle))


# ---------------------------------------------------------------------------
# 常用流程 / 数据构造（供各用例复用）
# ---------------------------------------------------------------------------

import time

_unique_counter = {"n": 0}


def unique_name(tag):
    _unique_counter["n"] += 1
    return "ui_%s_%d_%d" % (tag, int(time.time()), _unique_counter["n"])


def admin_token(ui):
    """管理员登录；失败返回 None（预置账号可能被改，交给用例自行 SKIP）。"""
    u, p = config.Accounts.ADMIN
    resp = ui.login(u, p)
    data = resp.json or {}
    return data.get("token") if data.get("ok") else None


def find_question(ui, token, title):
    """在 /all_questions 中按标题找题；返回 {id,title} 或 None。"""
    for q in list_questions(ui, token):
        if q["title"] == title:
            return q
    return None


def create_question(ui, token, payload):
    """发布题目并返回 id；失败抛错。"""
    resp = ui.post("/api/questions", payload, token=token)
    data = resp.json or {}
    if not data.get("ok") or not data.get("id"):
        raise AssertionError("create question 失败: %s" % data.get("message", resp.text))
    return str(data["id"])


def update_question(ui, token, qid, payload):
    resp = ui.put("/api/questions/%s" % qid, payload, token=token)
    data = resp.json or {}
    if not data.get("ok"):
        raise AssertionError("update question 失败: %s" % data.get("message", resp.text))


def delete_question(ui, token, qid):
    resp = ui.delete("/api/questions/%s" % qid, token=token)
    data = resp.json or {}
    if not data.get("ok"):
        raise AssertionError("delete question 失败: %s" % data.get("message", resp.text))


def acm_payload(title, answer_code, hidden_cases, visible_cases=None, rank="简单"):
    """构造 ACM（传统 IO）模式的题目请求体。hidden_cases: [{input, expected}, ...]"""
    return {
        "title": title,
        "rank": rank,
        "scope": "global",
        "desc": "输入/输出判题：读入 stdin，按要求输出。",
        "header": "",
        "answer": answer_code,
        "tail": "",
        "mode": "acm",
        "cpu_limit": 1,
        "mem_limit": 30,
        "visible_cases": visible_cases if visible_cases is not None else [],
        "hidden_cases": hidden_cases,
    }


def function_payload(title, header, answer, tail, hidden_cases, visible_cases=None, rank="简单"):
    """构造函数接口模式题目请求体。"""
    return {
        "title": title,
        "rank": rank,
        "scope": "global",
        "desc": "补全函数；输入/输出见题目格式。",
        "header": header,
        "answer": answer,
        "tail": tail,
        "mode": "function",
        "cpu_limit": 1,
        "mem_limit": 30,
        "visible_cases": visible_cases if visible_cases is not None else [],
        "hidden_cases": hidden_cases,
    }

