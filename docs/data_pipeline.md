Data pipeline: tick ingestion, conversion and 1m aggregation

Overview
--------
This folder contains tools to convert raw exchange tick JSON into compact packed binary and to aggregate ticks into 1-minute OHLCV files. The pipeline is optimized for storage and backtesting performance.

Tools
-----
- `tools/tick2bin` — read newline-delimited JSON from stdin and write packed binary ticks per symbol into `data/tick/<SYMBOL>/YYYY-MM.bin` (shard by month by default). Options:
  - `--symbol <SYM>` required
  - `--outdir <dir>` default `data/tick`
  - `--gzip` compress produced `.bin` files after processing
  - `--shard-day` use per-day shards instead of per-month
  - `--index-every N` write simple index entries every N records

- `tools/aggregate_1m` — read packed ticks (stdin binary or CSV-like lines) and write per-month `data/1m/<SYMBOL>/YYYY-MM.bin.gz` with packed OHLCV records (48 bytes per minute before compression).

- `tools/downloader` — simple rate-limited downloader using libcurl. Provide a file with one URL per line and an output directory. Options:
  - `--workers N` concurrent workers
  - `--delay-ms N` minimum delay between requests to the same host (ms)
  - `--retries N` number of retries per URL

Recommended workflow
--------------------
1. Create a URL list to download historical tick JSON files (one URL per line). Respect exchange API docs for epoch ranges.
2. Run `tools/downloader urls.txt /tmp/downloads --workers 4 --delay-ms 1000` to download files while respecting per-host minimum delay.
3. Convert downloaded JSON files to packed binary per symbol:

```bash
cat /tmp/downloads/file.json | build/tick2bin --symbol BTCUSDT --outdir data/tick --gzip
```

4. Aggregate to 1m OHLCV (from packed ticks or raw JSON):

```bash
cat /tmp/downloads/file.json | build/aggregate_1m BTCUSDT data/1m
```

API rate limiting
-----------------
- The downloader implements a per-host minimum delay (`--delay-ms`) which you should set according to the exchange's policy (e.g., 1200 ms). For API key authenticated endpoints, you should still respect returned rate-limit headers.
- For high-volume historical pulls, prefer requesting larger ranges per request (if API allows) rather than many small requests.

Notes & next steps
------------------
- Tools are basic and built for reliability over features. Production improvements: gz streaming output, robust JSON schema handling, sharded multi-threaded conversion, and a small SQLite index for random access.
- If you want, I can now run the pipeline on a small real sample from a URL you provide, or start bulk ingestion with your API keys (you will need to provide keys or allow me to use a configured environment).

Temporary disable of downloads
-----------------------------
- If you want to temporarily stop all automated/history downloads (for example before migrating to a new host), set the flag `disable_history_download` to `true` in `config/config.json` (default `false`).
- When enabled, the background `DataPublisher` will exit early and the `tools/downloader` executable will also refuse to run and print a message. This is a quick safety switch to avoid accidental large downloads.
