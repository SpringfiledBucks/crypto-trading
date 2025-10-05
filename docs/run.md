Run script template usage

This document explains how to use the `scripts/run.sh.template` provided in the
repository. The repository keeps only templates; local runtime artifacts and
installed systemd unit files should remain out of version control.

1) Create a local runnable script

Copy the template to a local run script and make it executable:

```bash
cp scripts/run.sh.template scripts/run.sh
chmod +x scripts/run.sh
```

2) Start/stop the service locally

```bash
# start both proxy monitor and app
scripts/run.sh start
# stop both
scripts/run.sh stop
# restart
scripts/run.sh restart
# status
scripts/run.sh status
```

3) Install user-level systemd units (optional)

If you want systemd to manage scheduled refreshes or timers, copy the
appropriate template files into your user systemd directory:

```bash
mkdir -p ~/.config/systemd/user
cp systemd/crypto_trading.service.template ~/.config/systemd/user/crypto_trading.service
cp systemd/crypto_trading_refresh.timer.template ~/.config/systemd/user/crypto_trading_refresh.timer
# reload and enable
systemctl --user daemon-reload
systemctl --user enable --now crypto_trading_refresh.timer
```

Adjust `%h` in the templates to absolute paths if you prefer system-level units
or want to run under a different user.

4) Notes
- The repo intentionally tracks only templates. Keep your local `scripts/run.sh`
  and any installed `~/.config/systemd/user/*.service` files out of git.
- Log archives are written under `archive/` and are ignored by default.
- `config/config.json` provides runtime flags such as `disable_history_download`.
