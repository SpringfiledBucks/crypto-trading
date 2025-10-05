# CI 集成建议（示例：GitHub Actions）

下面给出一个可复制的 CI 集成思路，适用于没有自有 CI 服务器的情况：可以直接使用 GitHub Actions 的免费 Runner，或在本地使用 `act` 进行测试。

目标：在 PR/主分支构建项目并运行 `tools/test_sse_client`（或 `scripts/test_sse.sh --build --`）作为回归测试，若失败则标记构建失败并上传日志供排查。

示例工作流（.github/workflows/ci.yml）

```yaml
name: CI
on: [push, pull_request]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libncurses5-dev libncursesw5-dev
      - name: Configure and build
        run: |
          mkdir -p build && cd build
          cmake ..
          make -j
      - name: Run SSE smoke test
        run: |
          cd build
          # run wrapper to build and execute the test tool
          ../scripts/test_sse.sh -- --timeout 10
        continue-on-error: false
      - name: Upload logs (artifact)
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: logs
          path: |
            logs/runtime.log
            logs/crypto_trading.out
```

要点说明
- 使用 `scripts/test_sse.sh` 作为包装器，可以保证在 CI 中既构建又执行 test 工具（避免依赖本地构建状态）。
- 将测试设为短超时（例如 10s），以便不会阻塞 Runner 太久；若需要更完整的集成测试，可把其放在单独的 longer job 中。
- 在失败时上传 `logs/` 中的日志为 artifact，方便查看具体的写入与 SSE 推送情况。

在没有 GitHub Actions 权限/不想公开仓库时的替代方案
- 使用 `act`（https://github.com/nektos/act）在本地模拟 GitHub Actions。可在本地机器上运行上面的工作流。注意需要在本地预装所需 apt 包。
- 使用轻量级 CI 提供商（例如 GitLab CI、CircleCI、Travis），思路类似：checkout -> install deps -> build -> run test wrapper -> 收集/上传日志。

安全/权限注意
- 测试工具会在仓库 `logs/` 下写入文件。CI Runner 也会把 artifact 上传到 workflow 页面；请确保没有敏感凭证写入这些日志（例如不要在 CI 中使用真实 API keys）。

我可以为你：
- 把上面示例工作流文件添加到 `.github/workflows/ci.yml`（如果你愿意我代替提交）；
- 或者生成一个针对私有仓库的更详细 CI 配置（包含并行 job、缓存依赖、以及更全面的集成测试）。
