## Web 控制台（Web Console）

本项目包含一个轻量的 SSE（Server-Sent Events）Web 控制台，用来实时查看运行时日志和结构化状态（state）。Web 控制台由 C++ 后端实现（`src/webconsole_server.cpp`），前端静态文件在 `webconsole/index.html`。

主要可用链接
- 根页面（仪表盘）： http://<host>:8080/ 或 http://127.0.0.1:8080/  （显示控制台面板与原始日志）
- SSE 事件流： http://<host>:8080/events  （事件类型：`state` — 结构化状态，raw log 行以 data: 前缀传输）
- 状态快照 JSON： http://<host>:8080/status  （直接返回当前内存中的 JSON 状态）

事件说明
- state
  - 内容：JSON 对象，包含字段：`ts`（Unix 时间戳）、`status`、`connection`、`subscriptions`（数组）、`orders`（数组或空）。
  - 来源：后端服务以 `STATE_JSON { ... }` 的形式写入日志，webconsole 将优先解析并广播该事件。
- raw log lines
  - /events 也会把日志的原始行（经清理后的文本）作为普通 data 行推送，供调试或回放使用。

如何运行
- 直接运行（开发/本地，使用 C++ 二进制）：
```bash
# 在 build 目录内构建并运行
mkdir -p build && cd build
cmake ..
make -j
./crypto_trading
```

- 使用 systemd（已在部署时安装）：
  - 服务名：`crypto_trading.service`（或部署时创建的等效 unit）
  - 启动： `sudo systemctl start crypto_trading.service`
  - 重启： `sudo systemctl restart crypto_trading.service`
  - 状态： `sudo systemctl status crypto_trading.service`
  - 日志： `journalctl -u crypto_trading.service -f`

如何调试/验证
- 查看当前状态 JSON：
  - curl -sS http://127.0.0.1:8080/status | jq .
- 订阅 SSE（命令行）：
  - curl -N http://127.0.0.1:8080/events

代理（如果需要）
- webconsole 可通过 systemd drop-in 注入代理环境变量（HTTP_PROXY/HTTPS_PROXY/ALL_PROXY/WS_PROXY），用于当部署环境需要转发到本地代理（例如 socks5/HTTP）时。

注意
- webconsole 会 tail `logs/runtime.log` 并解析 `STATE_JSON` 前缀的行来填充结构化状态；确保运行的 `crypto_trading` 已启用文件日志（环境变量 `LOG_TO_FILE=1` 或交互式运行）以便 webconsole 能读取到数据。

示例快速检查流程
1. 确保 trading 服务写入文件日志： `sudo systemctl show crypto_trading.service --property=Environment`，看是否包含 `LOG_TO_FILE=1`。
2. 启动/重启 webconsole： `sudo systemctl restart crypto_webconsole.service`。
3. 打开浏览器访问 `http://127.0.0.1:8080/`，或使用上面的 curl 命令查看 `/status` 与 `/events`。

更多信息参见项目根目录的 `README.md` 与 `docs/` 下其他文档。

外网连通性排查（当外网无法访问 http://<public-ip>:8080/ 时）
1. 确认服务已在本机启动并监听 8080：
   - `ss -ltnp | grep :8080` 或 `netstat -ltnp | grep :8080`
2. 在公网机器上运行 `curl -v http://<public-ip>:8080/`，注意是否能建立 TCP 连接以及 HTTP 层返回的响应；如果 curl 能建立连接但显示 `Empty reply from server`，说明 TCP 到达但应用层未正确响应或进程崩溃，请查看服务日志 `/tmp/webconsole.out`。
3. 检查宿主机是否处于 NAT/CGNAT/云提供商内网，ICMP 能通不代表 HTTP 可达；必要时联系网络提供商或配置公网转发。

自动刷新与节流策略
- 前端会根据所选的 K 线周期自动调整刷新频率：
  - 1m -> 5 秒
  - 30m -> 60 秒
  - 4h -> 5 分钟
- 目的是在常用的短周期下保证交互感受，同时避免在长周期下对外部 API 发起过多请求。
