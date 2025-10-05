# crypto-trading (C++ implementation)

这是一个 C++ 项目骨架，用来访问币安永续合约和期权市场数据并实现自动开平单的基础架构。包含一个基于 ncurses 的控制台监控页面。

构建依赖：
- CMake >= 3.10
- Compiler 支持 C++17
- libcurl
- ncurses
- nlohmann/json (single-header)

快速开始：
1. 安装依赖（Ubuntu 示例）：

```bash
sudo apt update
sudo apt install -y build-essential cmake libcurl4-openssl-dev libncurses5-dev libncursesw5-dev
```

2. 克隆/进入仓库并构建：

```bash
mkdir build && cd build
cmake ..
make -j
```

3. 运行：

```bash
./crypto_trading
```

更多部署与 systemd 配置说明，请参阅 `docs/DEPLOY.md`。

Web 控制台（轻量，C++ 后端）

本仓库使用 C++ 实现后端并通过 SSE 向前端推送运行时状态与日志（`STATE_JSON`）。

主要文件：
- 后端：`src/webconsole_server.cpp`（由 `build/crypto_trading` 启动，默认监听 8080）
- 前端静态文件：`webconsole/index.html`（前端只负责绘图与订阅 SSE）
- 调试工具：`tools/test_sse_client.cpp`（集成到 CMake，可用于本地回归测试 SSE 行为）

启动（编译并运行 C++ 二进制）：

```bash
# 在项目根目录下构建
mkdir -p build && cd build
cmake ..
make -j

# 运行 C++ 程序（包含 webconsole）
./crypto_trading
```

在浏览器中打开：

http://127.0.0.1:8080/

详细操作与部署信息请参见 `docs/WEBCONSOLE.md` 和 `docs/DEPLOY.md`。


配置文件说明（项目使用的配置文件）：

- `config/config.json`：主配置，包含交易所 API 基础 URL、交易选项（是否开启）、UI 刷新设置、以及代理设置等非敏感配置。
- `config/symbols.json`：交易对列表（已拆分为独立文件，以便快速管理交易对）。
- `config/secrets.json`：本地私密文件（仅示例，不应提交），可包含 `BINANCE_API_KEY` 与 `BINANCE_API_SECRET`；已在 `.gitignore` 中忽略。
- `config/CONFIG_TEMPLATE.md`：配置模板与字段说明（参考并复制到 `config/config.json` / `config/secrets.json`）。
- `scripts/run_template.sh`：启动脚本模板（不要将带有密钥的 `scripts/run.sh` 提交到仓库）。

归档与运行脚本说明：
- 非核心或 legacy 的脚本已移到 `archive/scripts/`，以保持主分支整洁（你仍可在归档中找到历史脚本）。
- 本地运行脚本 `scripts/run.sh` 可能包含敏感 API Key/Secret，已加入 `.gitignore`，不会被推送到远程。请确保在本地对该文件设置权限（600 或 700）。

注意：当前仓库为演示骨架，交易功能为占位实现。将真实交易按需实现并谨慎测试。

快速运行与测试
----------------
如果你只想快速在本机验证构建与 SSE 行为：

```bash
# 构建
mkdir -p build && cd build
cmake ..
make -j

# 运行主程序
./crypto_trading

# 在另一个终端运行 SSE 测试（脚本会构建并运行工具）
cd /path/to/repo
./scripts/test_sse.sh --build -- --timeout 10
```

工具说明
- `tools/test_sse_client.cpp`：C++ SSE 测试客户端（已参数化，可传 `--host/--port/--log/--delay/--timeout`）。
- `scripts/test_sse.sh`：脚本包装器，便于在 CI 或本地执行（会构建并运行 test_sse_client）。

CI / CD（快速说明）
-------------------
仓库已包含一个示例 GitHub Actions 工作流（`.github/workflows/ci-cd.yml`）和 CI 准备说明（`docs/CI_SETUP.md`）。工作流会在 push/PR 时构建并运行 SSE smoke test，并在合并到 `main` 或手动触发时执行可选的 deploy 步骤。

请参阅 `docs/CI_SETUP.md` 中的“你需要做的事”清单：
- 在 GitHub 仓库中添加 Secrets（`DEPLOY_SSH_PRIVATE_KEY`、`DEPLOY_USER`、`DEPLOY_HOST`、可选 `DEPLOY_PATH`）。
- 在目标服务器上把公钥加入 `authorized_keys`，并确保有合适的权限与 systemd 配置（如使用 deploy 功能）。

若你需要，我可以：
- 把 `ci-cd.yml` 改为仅做构建和测试（不部署），或把 deploy 设为手动触发；
- 帮你完成公钥部署并首次触发 workflow（需你在 GitHub 添加 Secrets）。

下一步
------
如果你有明确的下个目标（例如把订单执行逻辑接入 Binance、添加更多监控/指标、或把 SSE 写改为异步），告诉我，我会继续实现并把变更提交到仓库。
