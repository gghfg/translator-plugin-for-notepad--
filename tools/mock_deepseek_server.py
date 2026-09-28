#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
本地 mock 的 DeepSeek Chat Completions 服务，用于联调测试。

- 把收到的最后一个请求（路径 / 请求头 / 请求体）原样写到日志文件，便于核对
  请求体字段和 Authorization 头是否正确。
- 按 URL 路径返回不同场景，以便覆盖客户端的各条分支。

用法：
    python mock_deepseek_server.py [端口] [日志文件]
默认：端口 18080，日志 mock_request.json
"""

import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
LOG = sys.argv[2] if len(sys.argv) > 2 else "mock_request.json"

OK_BODY = {
    "id": "chatcmpl-mock",
    "object": "chat.completion",
    "model": "deepseek-chat",
    "choices": [
        {
            "index": 0,
            "message": {"role": "assistant", "content": "你好，世界。这是一次冒烟测试。"},
            "finish_reason": "stop",
        }
    ],
    "usage": {"prompt_tokens": 12, "completion_tokens": 9, "total_tokens": 21},
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # 静音默认访问日志

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(length) if length else b""

        record = {
            "path": self.path,
            "headers": {k: v for k, v in self.headers.items()},
            "body_raw": raw.decode("utf-8", "replace"),
        }
        try:
            record["body_json"] = json.loads(raw.decode("utf-8"))
        except Exception as exc:  # noqa: BLE001
            record["body_json_error"] = str(exc)

        with open(LOG, "w", encoding="utf-8") as fh:
            json.dump(record, fh, ensure_ascii=False, indent=2)

        if "/unauthorized" in self.path:
            status = 401
            payload = json.dumps(
                {"error": {"message": "Authentication Fails", "type": "authentication_error"}},
                ensure_ascii=False,
            ).encode("utf-8")
        elif "/emptychoices" in self.path:
            status = 200
            payload = json.dumps({"choices": []}).encode("utf-8")
        elif "/badjson" in self.path:
            status = 200
            payload = b"<html><body>this is not json</body></html>"
        elif "/slow" in self.path:
            # 故意拖时间，让客户端有机会在它返回之前把它取消掉
            time.sleep(2.5)
            status = 200
            payload = json.dumps(OK_BODY, ensure_ascii=False).encode("utf-8")
        else:
            status = 200
            payload = json.dumps(OK_BODY, ensure_ascii=False).encode("utf-8")

        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


if __name__ == "__main__":
    # 必须用 ThreadingHTTPServer：单线程的 HTTPServer 配上 HTTP/1.1 keep-alive
    # 会把并发请求串行化甚至卡死（取消语义那个测试就是两个请求并发）。
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    server.daemon_threads = True
    print(f"mock DeepSeek listening on http://127.0.0.1:{PORT}  log={LOG}", flush=True)
    server.serve_forever()
