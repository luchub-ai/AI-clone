#!/usr/bin/env python3
"""
Mock WebDriver server - gia lap 1 phan API cua chromedriver de test
WebBrowserTool ma KHONG can Chrome that. Cung tinh than voi
tests/tavily_mock_server.h dang dung cho WebSearchTool: dung 1 server gia
tra ve du lieu co kiem soat, thay vi phu thuoc dich vu that luc chay test.

Chi implement dung nhung endpoint WebBrowserTool goi toi:
  POST   /session                    -> tao session
  POST   /session/{id}/url           -> navigate
  GET    /session/{id}/title         -> lay tieu de
  POST   /session/{id}/execute/sync  -> chay script (dung cho read_text)
  DELETE /session/{id}               -> dong session

Them 1 endpoint debug KHONG co trong WebDriver that:
  GET /debug/state -> {"created_count": N, "active_count": M}
de test C++ (qua 1 lenh curl rieng, khong qua WebBrowserTool) co the kiem
tra session co duoc TAI SU DUNG giua nhieu lan navigate hay khong, va
DELETE co thuc su duoc goi luc destructor chay hay khong - day la hanh vi
STATEFUL rieng cua tool nay nen can kiem tra ro, khong chi kiem tra tung
request doc lap.
"""
import http.server
import json
import re
import sys
import uuid

CREATED_COUNT = 0
SESSIONS = {}  # sessionId -> {"url": str|None}

PAGES = {
    "https://example.com/machine-learning": {
        "title": "Machine Learning 101",
        "text": "Machine Learning la mot nhanh cua AI tap trung vao viec hoc tu du lieu. "
                "No khac lap trinh truyen thong o cho khong can viet rule tay.\n"
                "(noi dung mo phong cho bai test WebBrowserTool)",
    },
    "https://example.com/deep-learning": {
        "title": "Deep Learning Explained",
        "text": "Deep Learning dung mang no-ron nhieu tang de hoc dac trung tu du lieu tho. "
                "Day la nen tang cua hau het cac mo hinh AI hien dai.\n"
                "(noi dung mo phong cho bai test WebBrowserTool)",
    },
}
DEFAULT_PAGE = {"title": "Untitled Mock Page", "text": "(khong co noi dung mo phong cho url nay)"}


def lookup_page(url):
    """Tra ve page mo phong cho 1 url. Mot trinh duyet/server that coi SCHEME
    khong phan biet hoa/thuong (RFC 3986) - vd "HTTP://x" va "http://x" la
    CUNG 1 url. WebBrowserTool ban that da sua de chap nhan ca 2 dang (xem
    startsWithCaseInsensitive trong web_browser_tool.cpp), nen mock server
    o day cung phai chuan hoa scheme truoc khi tra cuu PAGES, khong thi test
    voi url viet hoa se bi mock server bao "khong tim thay trang" mot cach
    oan uong - LOI KHONG PHAI O CODE C++, MA O MOCK DON GIAN QUA.
    Day khong phai chuan hoa URL day du theo RFC 3986 (path/query van giu
    nguyen, dung cho pham vi test hien tai), chi du de mock hoat dong dung
    y nghia."""
    if url and "://" in url:
        scheme, rest = url.split("://", 1)
        url = scheme.lower() + "://" + rest
    return PAGES.get(url, DEFAULT_PAGE)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # tat access log mac dinh cho do roi console test

    def _read_json(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b""
        return json.loads(raw) if raw else {}

    def _send(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _session_or_404(self, sid):
        if sid not in SESSIONS:
            self._send(404, {"value": {"error": "invalid session id",
                                        "message": f"session {sid} khong ton tai"}})
            return False
        return True

    def do_POST(self):
        global CREATED_COUNT

        if self.path == "/session":
            _ = self._read_json()
            sid = uuid.uuid4().hex
            SESSIONS[sid] = {"url": None}
            CREATED_COUNT += 1
            self._send(200, {"value": {"sessionId": sid,
                                        "capabilities": {"browserName": "mock-chrome"}}})
            return

        m = re.match(r"^/session/([^/]+)/url$", self.path)
        if m:
            sid = m.group(1)
            body = self._read_json()
            url = body.get("url", "")
            if not self._session_or_404(sid):
                return
            if "mock-error.test/trigger-500" in url:
                self._send(500, {"value": {"error": "unknown error",
                                            "message": "gia lap loi server noi bo"}})
                return
            if "mock-error.test/trigger-400" in url:
                self._send(400, {"value": {"error": "invalid argument",
                                            "message": "gia lap url khong hop le"}})
                return
            SESSIONS[sid]["url"] = url
            self._send(200, {"value": None})
            return

        m = re.match(r"^/session/([^/]+)/execute/sync$", self.path)
        if m:
            sid = m.group(1)
            _ = self._read_json()
            if not self._session_or_404(sid):
                return
            page = lookup_page(SESSIONS[sid].get("url"))
            self._send(200, {"value": page["text"]})
            return

        self._send(404, {"value": {"error": "unknown command",
                                    "message": f"khong ho tro POST {self.path}"}})

    def do_GET(self):
        if self.path == "/debug/state":
            self._send(200, {"created_count": CREATED_COUNT, "active_count": len(SESSIONS)})
            return

        m = re.match(r"^/session/([^/]+)/title$", self.path)
        if m:
            sid = m.group(1)
            if not self._session_or_404(sid):
                return
            page = lookup_page(SESSIONS[sid].get("url"))
            self._send(200, {"value": page["title"]})
            return

        self._send(404, {"value": {"error": "unknown command",
                                    "message": f"khong ho tro GET {self.path}"}})

    def do_DELETE(self):
        m = re.match(r"^/session/([^/]+)$", self.path)
        if m:
            SESSIONS.pop(m.group(1), None)
            self._send(200, {"value": None})
            return
        self._send(404, {"value": {"error": "unknown command",
                                    "message": f"khong ho tro DELETE {self.path}"}})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9515
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"Mock WebDriver server dang chay tai 127.0.0.1:{port}", flush=True)
    server.serve_forever()