"""Small HTTP client for Epic's built-in MCP endpoint; no additional server/plugin."""
import json
import urllib.request


class NativeMCP:
    def __init__(self):
        self.session = None
        self.sequence = 0
        self.version = "2025-11-25"
        self.request("initialize", {"protocolVersion": self.version,
                     "capabilities": {}, "clientInfo": {"name": "Codex", "version": "1.0"}})
        self.request("notifications/initialized", notification=True)

    def request(self, method, params=None, notification=False):
        self.sequence += 1
        body = {"jsonrpc": "2.0", "method": method}
        if not notification:
            body["id"] = self.sequence
        if params is not None:
            body["params"] = params
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.session:
            headers.update({"Mcp-Session-Id": self.session, "MCP-Protocol-Version": self.version})
        request = urllib.request.Request("http://127.0.0.1:8000/mcp", json.dumps(body).encode(), headers)
        with urllib.request.urlopen(request, timeout=60) as response:
            self.session = response.headers.get("Mcp-Session-Id", self.session)
            if response.status in (202, 204):
                return None
            if "text/event-stream" in response.headers.get("Content-Type", ""):
                for line in response:
                    if line.startswith(b"data: "):
                        result = json.loads(line[6:])
                        if result.get("id") == self.sequence:
                            break
            else:
                result = json.load(response)
        if "error" in result:
            raise RuntimeError(result["error"])
        return result.get("result")

    def tool(self, name, arguments=None):
        return self.request("tools/call", {"name": name, "arguments": arguments or {}})


if __name__ == "__main__":
    client = NativeMCP()
    print(json.dumps(client.request("tools/list"), ensure_ascii=False, indent=2))
