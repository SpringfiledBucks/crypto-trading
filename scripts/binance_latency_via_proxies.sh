#!/usr/bin/env bash
# 中文注释：此脚本通过本地代理（HTTP 或 SOCKS5）对 Binance 合约 ping 接口做多次请求并把结果写入 CSV。
# 注意：脚本不会被我执行；请在你可以访问外网且代理已启动的主机上运行。

set -euo pipefail

# 配置区域（如需修改代理或目标 URL，在此处更改）
HTTP_PROXY_URL="http://127.0.0.1:15615"
# 注意：curl 的 --socks5-hostname 参数应只传 host:port，不要带 scheme
SOCKS_PROXY_HOSTPORT="127.0.0.1:15651"
# 仅用于输出展示（curl 使用 host:port，不带 scheme）
SOCKS_PROXY_URL="socks5://127.0.0.1:15651"
ENDPOINTS=("https://fapi.binance.com/fapi/v1/ping")
TRIALS=5
CSV_FILE="$(pwd)/binance_proxy_latency.csv"

# 确保 CSV 文件有标题（若不存在则写入）
if [ ! -f "$CSV_FILE" ]; then
  # 新版 CSV header 包含 http_code 字段
  echo "mode,endpoint,trial,success,time_s,http_code,exit_code" > "$CSV_FILE"
fi

echo "将对以下代理进行测速（仅通过代理，不做直接连接）："
echo "  HTTP 代理: $HTTP_PROXY_URL"
echo "  SOCKS 代理: $SOCKS_PROXY_URL"
echo "测试端点： ${ENDPOINTS[*]}"

for endpoint in "${ENDPOINTS[@]}"; do
  # HTTP 代理测试
  for i in $(seq 1 $TRIALS); do
    # 使用 HTTP 代理发起请求，正确捕获 curl 退出码和输出
    out=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" --proxy "$HTTP_PROXY_URL" --max-time 10 "$endpoint")
    rc=$?
    http_code=$(echo "$out" | awk '{print $1}')
    time_total=$(echo "$out" | awk '{print $2}')
    # 如果 curl 本身失败（例如无法连接代理），标记 http_code 为 000
    if [ $rc -ne 0 ]; then
      http_code="000"
      time_total=""
    fi
    success=false
    if [ "$http_code" = "200" ]; then
      success=true
    fi
  # 追加到 CSV（包含 http_code）
  echo "http_proxy,$endpoint,$i,$success,$time_total,$http_code,$rc" >> "$CSV_FILE"
  echo "[http_proxy] trial=$i endpoint=$endpoint success=$success time=$time_total code=$http_code exit=$rc"
  done

  # SOCKS5 代理测试
  for i in $(seq 1 $TRIALS); do
    out=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" --socks5-hostname "$SOCKS_PROXY_HOSTPORT" --max-time 10 "$endpoint")
    rc=$?
    http_code=$(echo "$out" | awk '{print $1}')
    time_total=$(echo "$out" | awk '{print $2}')
    if [ $rc -ne 0 ]; then
      http_code="000"
      time_total=""
    fi
    success=false
    if [ "$http_code" = "200" ]; then
      success=true
    fi
  echo "socks_proxy,$endpoint,$i,$success,$time_total,$http_code,$rc" >> "$CSV_FILE"
  echo "[socks_proxy] trial=$i endpoint=$endpoint success=$success time=$time_total code=$http_code exit=$rc"
  done
done

echo "测速完成，结果追加到： $CSV_FILE"
