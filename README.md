# Prerequisite
Based on Binance fix protocol, you need to install some files.

3 files needed. \
resources/config.ini (for release) \
resources/test_config.ini (for test)

resources/private.pem (pem key auth)

```
[auth]
md_address = {MARKET_DATA_ADDRESS}
oe_address = {ORDER_ENTRY_ADDRESS}
port = 9000
api_key = {API_KEY}
pem_file_path = resources/private.pem
private_password = {PRIVATE_PEM_FILE_PASSWORD}

[meta]
ticker = {YOUR_TICKER}

[risk]
max_order_size = 100
max_position = 1
max_loss = 100
```