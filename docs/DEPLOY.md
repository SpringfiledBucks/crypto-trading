# 部署与运行指南

此文档说明如何把 `crypto-trading` 程序作为 systemd 服务部署，以及如何在开发/交互式环境中运行和查看日志。

1) 安装与构建

```bash
sudo apt update
sudo apt install -y build-essential cmake libcurl4-openssl-dev libncurses5-dev libncursesw5-dev 
mkdir -p build && cd build
cmake ..
make -j
```

2) systemd 部署（推荐用于 production 或后台运行）

- 编辑仓库中的 `scripts/crypto_trading.service` 模板，确认 `ExecStart` 路径是构建后的二进制路径（例如 `/home/crypto/crypto-trading/build/crypto_trading`）。
- 复制到 systemd 并重载：

```bash
sudo cp scripts/crypto_trading.service /etc/systemd/system/crypto_trading.service
sudo systemctl daemon-reload
sudo systemctl enable --now crypto_trading
```

- 日志查看：

```bash
sudo journalctl -u crypto_trading -f
```

3) 环境变量说明

- `NO_UI=1`：禁止 ncurses UI（在 systemd/headless 环境中应该设置）。
- `TRADING_MODE=paper|live`：可选。若设置为 `live` 但未提供 API keys（`BINANCE_API_KEY`/`BINANCE_API_SECRET`），程序会降级为 paper 并在日志中说明。
- `BINANCE_API_KEY` / `BINANCE_API_SECRET`：用于 live 下单（请勿写入版本控制）。优先级：环境变量 > `config/secrets.json`。

4) 交互式运行（开发/调试）

- 直接运行二进制会尝试启用 ncurses UI（若有 TTY）并且 Logger 会将日志也写入 `logs/runtime.log`（交互式模式）。

```bash
./build/crypto_trading
tail -f logs/runtime.log
```

5) 日志与信号

- 程序现在统一使用 Logger（写到 stderr/journal），并支持 SIGTERM/SIGINT 优雅退出（会关闭 WebSocket 并退出主循环）。

6) 其他

- 归档的 legacy 脚本位于 `archive/scripts/`。若需要恢复某个脚本，可从归档中复制回 `scripts/` 并审查其内容。

验证 SSE（本地快速验证）

仓库包含一个用于测试 SSE 的工具 `tools/test_sse_client.cpp`，它会连到 `http://127.0.0.1:8080/events` 并模拟向 `logs/runtime.log` 注入 `STATE_JSON` 行，验证服务器是否把事件推送到客户端。

构建并运行：

```bash
cd build
make test_sse_client
./test_sse_client
```

若输出显示 `received=1`，说明 SSE 推送与日志 tailing 在当前环境下工作正常。

示例：使用自定义参数运行测试

```bash
# 指定主机与端口，以及日志路径与超时
./test_sse_client --host 127.0.0.1 --port 8080 --log /home/crypto/crypto-trading/logs/runtime.log --delay 2 --timeout 15
```
