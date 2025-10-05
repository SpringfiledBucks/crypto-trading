# CI/CD 准备与激活备忘（你需要完成的事项）

下面把把之前工作流（`.github/workflows/ci-cd.yml`）激活并能安全部署所需的所有步骤独立列出。请按顺序完成每一项，然后在最后触发一次 workflow（我会在必要时提供后续帮助）。

## 一、在 GitHub 仓库中添加 Secrets
在你的 GitHub 仓库页面（Settings -> Secrets and variables -> Actions -> New repository secret）中添加下列 Secrets：

- `DEPLOY_SSH_PRIVATE_KEY`
  - 说明：用于 CI/CD 在 runner 上 SSH 到目标部署主机的私钥（PEM 格式）。
  - 如何生成（在本地机器执行）：
    ```bash
    # 生成密钥（不设置密码）
    ssh-keygen -t rsa -b 4096 -C "ci-deploy@yourdomain" -f ./ci_deploy_key -N ""
    # 私钥内容拷贝到 DEPLOY_SSH_PRIVATE_KEY
    cat ./ci_deploy_key
    # 公钥用于放到目标服务器的 authorized_keys
    cat ./ci_deploy_key.pub
    ```
- `DEPLOY_USER`：部署用户名（例如 `ubuntu`、`deployer`）
- `DEPLOY_HOST`：部署主机（IP 或域名，例如 `203.0.113.5`）
- `DEPLOY_PATH`（可选）：部署目录（例如 `/home/deployer/crypto-trading`）；若不提供工作流使用默认值 `/home/${DEPLOY_USER}/crypto-trading`。

注意：不要把私钥或 secrets 放在仓库中，必须通过 GitHub Secrets 界面或 `gh secret` 设置。

## 二、在目标服务器上准备（仅当你要使用 deploy job 时）
在目标服务器上执行以下步骤（以 `DEPLOY_USER` 账户或有 sudo 权限的账户）：

1. 创建部署用户（如果还没有）：
   ```bash
   sudo adduser --disabled-password --gecos "" deployer
   sudo usermod -aG sudo deployer   # 仅当需要 sudo 权限
   ```

2. 将 CI 生成的公钥（`ci_deploy_key.pub`）加入到 `/home/deployer/.ssh/authorized_keys`：
   ```bash
   sudo -u deployer mkdir -p /home/deployer/.ssh
   sudo -u deployer bash -c 'cat >> /home/deployer/.ssh/authorized_keys' < ci_deploy_key.pub
   sudo chmod 700 /home/deployer/.ssh
   sudo chmod 600 /home/deployer/.ssh/authorized_keys
   ```

3. 确保目标目录存在（工作流会创建，但建议预先确认权限）：
   ```bash
   sudo -u deployer mkdir -p /home/deployer/crypto-trading/build
   ```

4. systemd 单元（可选）：
   如果你希望工作流在部署后自动重启服务，请把 `scripts/crypto_trading.service` 拷到服务器并 `sudo systemctl enable --now crypto_trading.service`（只需首次设置）。确保 `ExecStart` 指向最终部署的二进制路径（例如 `/home/deployer/crypto-trading/build/crypto_trading`）。

## 三、本地验证（在你设置 Secrets 之前或不想立刻部署）
你可以在本地或开发机先验证构建与 SSE 测试：

```bash
# 构建
mkdir -p build && cd build
cmake ..
make -j

# 运行 SSE 测试（使用脚本包装器）
../scripts/test_sse.sh --build -- --timeout 10
```

如果测试显示 `received=1`，说明本地构建与 SSE 行为正常。

## 四、激活工作流（在 GitHub 上）
1. 在 GitHub 仓库的 Settings -> Secrets and variables -> Actions 中添加上面提到的 secrets。
2. 把更改 push 到远程仓库（若你的工作流已经存在于 main 分支，推送任意 commit 到 main 或建立 PR 都会触发 `build-and-test` job）：
   ```bash
   git add . && git commit -m "ci: activate workflow" || true
   git push origin main
   ```
3. 在 GitHub -> Actions 中查看 `CI / CD` 工作流执行情况；如果部署成功，`deploy` job 会把二进制 rsync 到目标服务器并尝试重启 systemd 服务。

手动触发（如果你已经设置 secrets）：
- 通过 GitHub UI：Actions 页面 -> 选择 workflow -> Run workflow（手动触发）。
- 或使用 `gh` CLI：
  ```bash
  gh workflow run ci-cd.yml -f
  ```

## 五、如果你不希望自动部署
如果你暂时只想启用 CI 构建/测试，而不自动把二进制部署到服务器，请告诉我，我可以把 `deploy` job 注释掉或改为 `if: github.event_name == 'workflow_dispatch'`（仅手动触发）。

## 六、回滚与调试建议
- 若构建/测试失败：在 GitHub Actions 的失败工作流页面，下载 Artifact（logs）并检查 `logs/runtime.log` 与 `logs/crypto_trading.out`。
- 若 deploy 失败：检查 Actions 日志中的 rsync/ssh 错误，并在服务器上 `/var/log/syslog` 或 `journalctl -u crypto_trading` 查看服务日志。

## 七、我可以帮你做的三件事（你可以选择）
1. 帮你把上面生成的公钥的公钥文本拷贝到目标服务器（如果你给我临时访问权限/公钥），或生成命令你自己执行。
2. 若你允许，我可以把 workflow 修改为**只做构建+测试**（去掉自动 deploy），并提交变更。
3. 我可以在你添加 Secrets 后帮助你第一次触发并观察工作流结果（你需要在 GitHub 上授权或把运行结果贴回，我会分析）。

完成以上步骤后，CI/CD 就能可靠运行并在合并到 main 时自动部署（如果你选择启用 deploy）。
