#!/usr/bin/env bash
# Template run script for crypto_trading
# Save a copy as scripts/run.sh (this file is ignored by git) and fill in your API keys.

# Option A: use environment variables (recommended)
# Uncomment and set your keys here OR export them in your shell/CI environment.
# export BINANCE_API_KEY="your_api_key_here"
# export BINANCE_API_SECRET="your_api_secret_here"

# Option B: use a local secrets file (also ignored)
# Create config/secrets.json with the following structure and chmod 600 it:
# {
#   "BINANCE_API_KEY": "your_api_key_here",
#   "BINANCE_API_SECRET": "your_api_secret_here"
# }
# Then uncomment the eval line below if you prefer this method.
# eval $(jq -r 'to_entries | .[] | "export \(.key)=\(.value)"' config/secrets.json)

# Start the program (ensure you've built it first)
# You can pass arguments to the binary as needed.
./build/crypto_trading
