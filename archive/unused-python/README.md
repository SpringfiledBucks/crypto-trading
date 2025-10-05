# archive/unused-python

此目录用于保存从 `scripts/` 目录中移除的临时/测试脚本的备份副本，目的是在不将这些文件保留在主仓库历史中（或阻止未来误提交）时，仍能在工作区保留一份本地备份以便恢复或参考。

重要说明
- 这些备份已从仓库索引中移除（已添加到 `.gitignore`），因此它们不会被纳入未来的提交或推送。
- 你可以在本地工作区中查看这些文件，但它们不再是版本控制的一部分（除非手动恢复）。

包含的文件（示例）
- asyncclient_create_test.py
- binance_latency_test.sh
- binance_latency_via_proxies.sh
- monitor_refresh.sh
- test_sse.sh
- twm_test.py
- ws_proxy_test.py
- legacy_tests/ (若干 legacy 脚本)

为什么归档
- 这些脚本大多数为临时测试/诊断用途，保留在 `scripts/` 会使主脚本目录混乱。
- 将它们移出并保留备份可以降低误用风险，同时保留恢复路径。

如何恢复某个文件
1. 若要从最近的提交历史恢复某个文件（如果需要），可以使用 git 历史命令查找包含文件的提交：
   ```bash
   git log --all --name-only -- archive/unused-python | sed -n '1,200p'
   ```
2. 或者检查合并该归档的合并提交（例如本仓库最近的合并提交）：
   ```bash
   git checkout <COMMIT_SHA> -- archive/unused-python/path/to/file
   ```
   上述命令会把文件恢复到当前工作树中，然后你可以把它移动回 `scripts/` 并提交到新分支以恢复为版本控制文件。

注意事项
- 如果你希望这些备份长期保存到另一个位置（例如系统备份目录或外部存储），建议把这些文件复制到非仓库路径（例如 `/home/crypto/backup-scripts/`）。
- 如果需要把这些文件彻底从 Git 历史中删除（例如包含敏感信息），那需要运行历史重写（`git filter-repo` 或 `git filter-branch`），这是破坏性的操作，请提前确认并备份仓库。

创建者/时间
- 由仓库维护脚本（自动化助手）在 2025-10-06 生成并提交（参见最新提交记录）。

如果你想要我把这些备份移动到一个非仓库目录或把 README 内容调整为更详细的文件清单，请告诉我要执行的操作。
