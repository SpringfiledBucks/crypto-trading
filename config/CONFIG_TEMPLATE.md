# Configuration template for crypto-trading (C++)

请把配置复制一份到 `config/config.json` 并填写你的值。配置项说明如下：

注意：为了更方便管理，交易对（symbols）已被拆分到 `config/symbols.json`，请在该文件中列出所有订阅的交易对。

敏感信息（API Key / Secret）请放入 `config/secrets.json` 或使用环境变量，切勿提交到仓库。

{
  "websocket": {
    "perpetual_base_url": "wss://fstream.binance.com/ws",
    "options_base_url": "wss://dstream.binance.com/ws",
    "subscriptions": [
      {
        "exchange": "binance",
        "market": "perpetual",
        "symbol": "BTCUSDT",
        "streams": ["markPrice", "kline_1m"]
      }
    ]
  },
  "trading": {
    "enabled": false,                 # 开启交易前务必测试和审查
    "mode": "paper",                # paper | live
    "api_key": "<你的API Key>",
    "secret": "<你的API Secret>",
    "account_type": "perpetual",    # perpetual | options
    "default_leverage": 20,
    "risk_per_trade": 0.01
  },
  "ui": {
    "refresh_ms": 500
  },
  "logging": {
    "level": "info",
    "path": "logs/"
  }
  ,
  "proxy": {
    "http": "http://127.0.0.1:3128",
    "https": "http://127.0.0.1:3128",
    "ws": "ws://127.0.0.1:8080"
  }
}

必需字段说明：
- websocket.perpetual_base_url: 币安永续 WebSocket 基础地址。
- websocket.options_base_url: 币安期权/交割合约相关 WebSocket 基础地址（如适用）。
- websocket.subscriptions: 订阅数组，每项包含 exchange、market、symbol 与要订阅的 streams（例如 markPrice、kline_1m、depth）。
- trading.enabled: 是否开启自动交易（false 表示仅监控）。
- trading.mode: paper(模拟) 或 live(真实)。
- trading.api_key / trading.secret: 在 live 模式下用于签名下单 API（不要把这些信息提交到版本控制）。
- ui.refresh_ms: 控制台 UI 刷新间隔（毫秒）。

安全注意：
- 请使用系统环境变量或受限配置文件来存储真实 API 密钥，切勿将密钥推送到远程仓库。可在未来将 config.json 支持引用环境变量的语法。

示例：

```json
{
  "websocket": {
    "perpetual_base_url": "wss://fstream.binance.com/ws",
    "options_base_url": "wss://dstream.binance.com/ws",
    "subscriptions": [
      {"exchange":"binance","market":"perpetual","symbol":"BTCUSDT","streams":["markPrice","kline_1m"]},
      {"exchange":"binance","market":"perpetual","symbol":"ETHUSDT","streams":["markPrice"]}
    ]
  },
  "trading": {"enabled":false,"mode":"paper","api_key":"","secret":"","account_type":"perpetual","default_leverage":20,"risk_per_trade":0.01},
  "ui": {"refresh_ms":500},
  "logging": {"level":"info","path":"logs/"}
  ,"proxy": {"http":"","https":"","ws":""}
}
```
