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
max_order_size = {TICKER_ORDER_QTY_SIZE}
max_position = {TICKER_ORDER_QTY_SIZE}
min_position = {TICKER_ORDER_QTY_SIZE}
max_loss = {PERCENT_OF_LOSS}

[main_init]
mud_pool_size = {MARKET_UPDATE_DATA_MEMORYPOOL_SIZE}
md_pool_size = {MARKET_DATA_UNIT_MEMORYPOOL_SIZE}
response_memory_size = {RESPONSE_MANAGER_MEMORYPOOL_SIZE}

[orders]
min_replace_qty_delta = {MINIMUM_QTY_DIFF}
min_replace_tick_delta = {MINIMUM_TICK_DIFF}

[log]
level = {LOG_LEVEL_IN_CAPITAL}
size = {LOG_ROTATION_MAX_SIZE}

[meta]
level = {DEPTH_LEVEL_IN_BINANCE}
ticker = {TICKER}
ticker_size = {TICKER_TICK_SIZE}
qty_precision = {QTY_PRECISION_IN_FIX_API}
price_precision = {PRICE_PRECISION_IN_FIX_API}

[risk]
max_order_size = {ONE_ORDER_ALLOWED_QTY}
max_position = {MAX_POSITION_QTY}
min_position = {MIN_POSITION_QTY}
max_loss = {DECIMAL_POINT_OF_LOSS}

[venue]
minimum_order_usdt = {MINIUM_ORDER_PRICE_QTY}
minimum_order_qty = {MINIUM_ORDER_QTY}
minimum_order_time_gap = {MININUM_GAP_BETWEEN_ORDERS}

[strategy]
algorithm = {NOT_USED_NOW}
position_variance = {MAKER_POSITION_VARIANCE}
variance_denominator = {MAKER_VARIANCE_DENOMINATOR}
enter_threshold = {MAKER_ENTER_THRESHOLD}
exit_threshold = {MAKER_EXIT_THRESHOLD}

[cpu_init]
clock = {CPU_BASE_CLOCK}
interval = {CPU_RECALCULATION_INTERVAL}

[cpu_id]
use_cpu_group = 1
use_cpu_to_tid = 1
count = 5

[thread]
count = 7

[cpu_0]
use_irq = 0
cpu_type = 2

[cpu_1]
use_irq = 0
cpu_type = 2

[cpu_2]
use_irq = 0
cpu_type = 2

[cpu_3]
use_irq = 0
cpu_type = 2

[cpu_4]
use_irq = 0
cpu_type = 2

[thread_0]
name = MDWrite
cpu_id = 0
prio = 40

[thread_1]
name = MDRead
cpu_id = 0
prio = 60

[thread_2]
name = OERead
cpu_id = 1
prio = 50

[thread_3]
name = OEWrite
cpu_id = 1
prio = 50

[thread_4]
name = TEMarketData
cpu_id = 2
prio = 99

[thread_5]
name = TEResponse
cpu_id = 3
prio = 99

[thread_6]
name = Logger
cpu_id = 4
prio = 50

```


# INI Checker
Run ini_checker python file to verify that the config.ini file matches all code which uses ini configuration values.
```
python3 ini_checker.py --ini resources/config.ini --root hft
```
