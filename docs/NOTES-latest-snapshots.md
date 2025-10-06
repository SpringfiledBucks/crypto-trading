Notes: data/latest price snapshots
---------------------------------

To avoid accidental collisions between short-lived price snapshot files and the
authoritative per-symbol files used by batch tools, short snapshots are now written
to `data/latest/price_snapshots/<SYMBOL>.json` by the `DataPublisher`.

Legacy tiny files matching `data/latest/*.latest.json` have been archived to
`archive/legacy/latest_legacy_<timestamp>.tar.gz` and moved to
`data/latest/legacy/` to prevent mis-detection by `--all` scans and other tools.

If you run maintenance scripts that iterate `data/latest/` for authoritative
symbol files, please ensure they skip `price_snapshots/` and `legacy/` subdirectories.

Suggested follow-ups:
- Update tooling or scripts that glob `data/latest/*.json` to explicitly only
  accept files matching `^[A-Z0-9]+\.json$` or iterate `data/latest` and filter
  out `price_snapshots` and `legacy` directories.
- Consider adding an automated cleanup job if you want to prune old snapshots.
