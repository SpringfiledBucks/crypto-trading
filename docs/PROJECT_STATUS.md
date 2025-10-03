项目当前状态与待办（由自动化助手生成）

一、环境变量优先级
- 程序优先使用启动脚本或环境中的变量：`BINANCE_API_KEY`、`BINANCE_API_SECRET`、`HTTP_PROXY`、`HTTPS_PROXY`、`WS_PROXY`。
- 若环境变量不存在，程序会回退读取 `config/secrets.json`（敏感文件，已在 `.gitignore` 中忽略）和 `config/config.json` 的代理字段（HTTP/HTTPS）。

二、已实现的主要功能
- CMake 构建系统与可执行文件 `build/crypto_trading`（已能构建）。
- HTTP 客户端：`HttpClient`（libcurl 封装），支持通过 `CURLOPT_PROXY` 使用 `socks5h://` 或 `http://` 代理。
- WebSocket 客户端：`WebSocketClient`（Boost.Beast TLS），支持 HTTP CONNECT 代理（从环境变量 `WS_PROXY` 读取）。
- Binance API：HMAC-SHA256 签名、serverTime JSON 解析、基础 signed REST 请求发送。
- OrderManager：paper/live 模式切换（若 env 提供 key 则 live），具有基本重试逻辑。
- 配置分离：`config/config.json`（非敏感）与 `config/symbols.json`（交易对）已分离。
- secrets 支持：若无 env，程序会读取 `config/secrets.json`（该文件被忽略，不应提交）。
- 控制台 UI：基本 ncurses UI，能显示状态与订阅列表（最小实现）。

三、部分实现或未实现的功能
- 动态 WebSocket 订阅管理：尚未自动把 `config/symbols.json` 转为完整的 WebSocket subscribe 消息并管理订阅生命周期。
- WebSocket SOCKS5 支持：当前只实现了 HTTP CONNECT 隧道；若你的 WS 代理是 SOCKS5，需要在客户端实现 SOCKS5 CONNECT 或通过本地转发器桥接。
- 完整下单功能：市价/限价/撤单/查询等高级下单逻辑需补全与测试。
- 更鲁棒的速率限制处理：当前为简单重试/睡眠策略，需实现令牌桶或指数回退以应对 429/418 等。
- 订单持久化与重启恢复：未实现。
- 日志与告警：未集成结构化日志库与告警机制。
- 单元/集成测试与 CI：缺失。

四、建议的下一步（可选）
1. 让 `WebSocketClient` 支持从 `config/config.json` 回退读取 `proxy.ws`（当前仅读取 env）。
2. 为 `WebSocketClient` 增加 SOCKS5 CONNECT 支持或提供桥接脚本。
3. 自动根据 `config/symbols.json` 生成并管理 WebSocket 订阅消息。
4. 扩展 OrderManager：增加更多订单类型、改进速率限制及持久化。
5. 集成日志（如 spdlog）和测试套件。

五、如何安全运行
- 推荐在本地使用 `scripts/run.sh`（该文件在 `.gitignore` 中），在脚本内 `export` 环境变量并把密钥设为文件权限 600/700。示例脚本已经位于 `scripts/run.sh`。
- 也可以把密钥放入 `config/secrets.json`（权限 600），但请勿提交到仓库。

生成时间：2025-10-03
