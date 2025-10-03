# Cleanup / Archive Suggestions

下面是我建议归档或删除的文件/目录（操作前请确认备份）：

- archive/ - 已存在的历史脚本归档（保留）
- scripts/*.sh - 保留必要的启动模板，legacy 测试脚本可移动到 `archive/scripts/`（已经部分移动）
- data/archive/ - 如果历史数据已备份到外部存储，可归档到长期存储
- logs/runtime.log - 作为运行时交互日志保留，若用于 systemd 建议改为 journal，只在交互式会话启用此文件

操作建议：
1. 先在分支上执行一次移动，把不再需要的脚本移动到 `archive/` 并保留 README 注释说明来源。
2. 清理大型二进制或编译产物（如 build/）不要提交到仓库；使用 .gitignore 忽略。
3. 对 `scripts/run.sh` 等包含密钥的示例文件，保留模板 `scripts/run_template.sh`，把真实 `scripts/run.sh` 仅保留在本地开发机。
