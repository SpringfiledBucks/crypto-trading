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

配置文件说明（项目使用的配置文件）：

- `config/config.json`：主配置，包含交易所 API 基础 URL、交易选项（是否开启）、UI 刷新设置、以及代理设置等非敏感配置。
- `config/symbols.json`：交易对列表（已拆分为独立文件，以便快速管理交易对）。
- `config/secrets.json`：本地私密文件（仅示例，不应提交），可包含 `BINANCE_API_KEY` 与 `BINANCE_API_SECRET`；已在 `.gitignore` 中忽略。
- `config/CONFIG_TEMPLATE.md`：配置模板与字段说明（参考并复制到 `config/config.json` / `config/secrets.json`）。
- `scripts/run_template.sh`：启动脚本模板（不要将带有密钥的 `scripts/run.sh` 提交到仓库）。

归档与运行脚本说明：
- 非核心或 legacy 的脚本已移到 `archive/scripts/`，以保持主分支整洁（你仍可在归档中找到历史脚本）。
- 本地运行脚本 `scripts/run.sh` 可能包含敏感 API Key/Secret，已加入 `.gitignore`，不会被推送到远程。请确保在本地对该文件设置权限（600 或 700）。

注意：当前仓库为演示骨架，交易功能为占位实现。将真实交易按需实现并谨慎测试。需要我为你：
- 接入 Binance REST 签名并实现下单/撤单 API。
- 添加 WebSocket 客户端以获取实时 K 线与深度。
- 编写单元测试与 CI 配置。

告诉我下一步需要实现的功能，我会继续把它做完整。
