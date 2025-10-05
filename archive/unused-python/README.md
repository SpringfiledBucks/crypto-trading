# archive/unused-python

此目录用于保存从 `scripts/` 目录中移除的临时/测试脚本的备份副本，目的是在不将这些文件保留在主仓库历史中（或阻止未来误提交）时，仍能在工作区保留一份本地备份以便恢复或参考。

重要说明
- 这些备份已从仓库索引中移除（已添加到 `.gitignore`），因此它们不会被纳入未来的提交或推送。
- 你可以在本地工作区中查看这些文件，但它们不再是版本控制的一部分（除非手动恢复）。

当前备份文件（位于工作区，但被 .gitignore 忽略）
- archive/unused-python/asyncclient_create_test.py
- archive/unused-python/binance_latency_test.sh
- archive/unused-python/binance_latency_via_proxies.sh
- archive/unused-python/monitor_refresh.sh
- archive/unused-python/test_sse.sh
- archive/unused-python/twm_test.py
- archive/unused-python/ws_proxy_test.py
- archive/unused-python/legacy_tests/
   - long_warm_sample.py
   - parse_latency_csv.py
   - proxy_latency_breakdown.py
   - warm_latency_test.py
   - ws_latency_test.py

为什么归档
- 这些脚本大多数为临时测试/诊断用途，保留在 `scripts/` 会使主脚本目录混乱。
- 将它们移出并保留备份可以降低误用风险，同时保留恢复路径。

如何恢复某个文件（两种常见方法）

方法 A — 从最近的包含提交中恢复（推荐用于单文件恢复）
1. 找到包含该文件的提交：
   ```bash
   git log --all --pretty=format:'%h %ad %s' --date=short -- archive/unused-python/path/to/file
   ```
2. 从该提交检出文件到当前工作树：
   ```bash
   git checkout <COMMIT_SHA> -- archive/unused-python/path/to/file
   # 然后把文件移回 scripts/ 并提交到新分支：
   git switch -c restore/<file-name>
   mv archive/unused-python/path/to/file scripts/
   git add scripts/path/to/file
   git commit -m "chore: restore scripts/<file> from backup"
   git push -u origin restore/<file>
   ```

方法 B — 直接从工作区复制（如果文件仍在本地）
1. 直接复制到新的保存位置（非仓库目录），例如：
   ```bash
   mkdir -p /home/crypto/backup-scripts
   cp -a archive/unused-python/* /home/crypto/backup-scripts/
   ```
2. 如果要重新将文件纳回版本控制，请把文件移回 `scripts/` 并提交到新分支，如上所示。

注意事项
- README 已被强制加入仓库，但 `archive/unused-python/` 目录本身被加入 `.gitignore`，所以除了 README 外，其他备份文件不会被再次跟踪。
- 如果你希望这些备份长期保存到另一个位置（例如系统备份目录或外部存储），建议把这些文件复制到非仓库路径（例如 `/home/crypto/backup-scripts/`）。
- 如果需要把这些文件彻底从 Git 历史中删除（例如包含敏感信息），那需要运行历史重写（`git filter-repo` 或 `git filter-branch`），这是破坏性的操作，请提前确认并备份仓库。

创建者/时间
- 由仓库维护脚本（自动化助手）在 2025-10-06 生成并提交（参见最新提交记录）。

如果你想要我把这些备份移动到一个非仓库目录或把 README 内容调整为更详细的文件清单，请告诉我要执行的操作。
