# Prerequisite

## System Dependencies
Install required system packages:
```bash
sudo apt install -y libtbb-dev libcurl4-openssl-dev
```

## Linux Account Setting
To allow to change cpu schduler policy([scheduler](https://linux.die.net/man/2/sched_getscheduler)) and nice([nice](https://linux.die.net/man/2/nice)), you need PAM
```shell
1. Check pam_limits.so is loaded
$ grep -n 'pam_limits.so' /etc/pam.d/common-session /etc/pam.d/common-session-noninteractive /etc/pam.d/sshd
/etc/pam.d/sshd:40:session    required     pam_limits.so

If you use sshd, activate PAM
echo 'UsePAM yes' |sudo tee /etc/ssh/sshd_config

2. Add your limit allowance
PRIORITY='priority what you want(1~99)'
echo "$USER - rtprio $PRORITY" | sudo tee /etc/security/limits.d/99-rtprio.conf

NICE='nice value what you want (-20~19). Higher number is lower priority'
echo "$USER - nice $NICE" | sudo tee /etc/security/limits.d/99-nice.conf

3.Check limits
rt
$ ulimit -r
99

nice
$ ulimit -e
40

```

## Set systemd for cgroup

For detailed instructions on setting up the HFT engine as a systemd service with CPU isolation, see [util/README.md](util/README.md).

Quick setup:
1. Configure iso.slice for CPU isolation
2. Install the HFT service using provided service file
3. CPU isolation is managed automatically on service start/stop

See [util/README.md](util/README.md) for complete documentation.

## CPU Monitoring

Check which processes are running on each CPU core:
```bash
ps -eo pid,comm,psr --no-headers | sort -k3 -n | awk '{cpu[$3] = cpu[$3] $1 " " $2 "\n"} END {for (c in cpu) {print "=== CPU " c " ==="; print cpu[c]}}'
```

Check current CPU isolation status:
```bash
# Check iso.slice allowed CPUs
systemctl show iso.slice | grep AllowedCPUs

# Check kernel boot parameters for CPU isolation
cat /proc/cmdline | tr ' ' '\n' | grep -E "isolcpus|nohz_full|rcu_nocbs"

# Check isolated CPUs (kernel level)
cat /sys/devices/system/cpu/isolated
```

Check CPU usage per core:
```bash
mpstat -P ALL 1 3
```

Based on Binance fix protocol, you need to install some files.

3 files needed. \
resources/config.ini (for release) \
resources/test_config.ini (for test)

resources/private.pem (pem key auth)

# FIX8 Schema Compilation

To compile FIX protocol schemas using fix8, navigate to the schema directory and run:

```bash
cd hft/core/NewOroFix44

# Compile Market Data schema
f8c -p NewOroFix44MD -n NewOroFix44MD -o . fix-md.xml

# Compile Order Entry schema
f8c -p NewOroFix44OE -n NewOroFix44OE -o . fix-oe.xml
```

This generates the necessary C++ source files (*_types.hpp, *_types.cpp, *_traits.cpp, *_router.hpp, *_classes.hpp, *_classes.cpp) for FIX protocol handling.

# config.ini example
Need resources/config.ini file like below.
Thread name is required for cpu affinity.

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
algorithm = {STRATEGY_NAME: maker or taker}
position_variance = {MAKER_POSITION_VARIANCE}
variance_denominator = {MAKER_VARIANCE_DENOMINATOR}
enter_threshold = {MAKER_ENTER_THRESHOLD}
exit_threshold = {MAKER_EXIT_THRESHOLD}
vwap_size = {VWAP_WINDOW_SIZE}
obi_level = {ORDER_BOOK_IMBALANCE_LEVEL}

[cpu_init]
clock = {CPU_BASE_CLOCK}
interval = {CPU_RECALCULATION_INTERVAL}

[cpu_id]
use_cpu_group = 1
use_cpu_to_tid = 1
count = 5

[thread]
count = 6

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
name = TradeEngine
cpu_id = 2
prio = 99

[thread_5]
name = Logger
cpu_id = 3
prio = 50

```


# INI Checker
Run ini_checker python file to verify that the config.ini file matches all code which uses ini configuration values.
```
python3 ini_checker.py --ini resources/config.ini --root hft
```


# HFT program Health checker with supervisord
sudo auth needed.
1. Install supervisord

#### linux
```
sudo apt install supervisord
```

2. Register service
```Shell
# Add slack webhook in supervisord
Environment=SLACK_WEBHOOK_URL=https://hooks.slack.com/services/XXX/YYY/ZZZ

# Copy execute file
pushd /opt/supervisor/
sudo ln -s ${YOUR_PROJECT_BUILD_PATH}/HFT .
popd

# Copy slack notifier file
pushd /opt/supervisor/listeners/
sudo ln -s ${YOUR_PROJECT_PATH}/util/slack_notifier.py .
popd

# Register supervisord files
pushd /etc/supervisor/conf.d/
sudo ln -s ${YOUR_PROJECT_PATH}/util/hft-supervisord.conf .
sudo ln -s ${YOUR_PROJECT_PATH}/util/slack_notifier.conf .
popd

# Init supervisorctl and check its status
sudo supervisorctl reread
sudo supervisorctl update
sudo supervisorctl status

# Stop hft
sudo supervisorctl stop hft
```

# ASAN Test
```shell
cmake -S . -B test-build -DENABLE_ASAN=ON
cmake --build test-build -j
ASAN_OPTIONS=allocator_may_return_null=0:detect_leaks=1 ctest --test-dir test-build/test -V
```

# Strategy System

The HFT system supports dynamic strategy loading via configuration. Strategies are selected at runtime based on the `algorithm` field in `config.ini`:

Available strategies:
- `maker`: Market maker strategy
- `taker`: Liquidity taker strategy

To add a new strategy:
1. Create strategy class inheriting from `BaseStrategy`
2. Implement `on_orderbook_updated()`, `on_trade_updated()`, `on_order_updated()`
3. Create registration function using `Registrar<YourStrategy>`
4. **Add registration call to `register_all_strategies()` in `strategies.hpp`**

Example `strategies.hpp`:
```cpp
#include "liquid_taker.h"
#include "market_maker.h"
#include "your_new_strategy.h"  // Add your strategy header

namespace trading {
inline void register_all_strategies() {
  register_market_maker_strategy();
  register_liquid_taker_strategy();
  register_your_new_strategy();  // Add your registration function
}
}
```

# Testing Notes

When writing tests that use `Logger`, declare the Logger instance globally or as a static fixture member to avoid thread-local storage issues with ProducerTokens. Logger uses thread_local storage for performance optimization, so creating and destroying Logger instances within individual test scopes can cause use-after-free issues.

**Important**: Static member variables must be defined outside the class declaration to avoid linker errors (`undefined reference`).

Example:
```cpp
class NeworoTest : public ::testing::Test {
 protected:
  static std::unique_ptr<Logger> logger;
  void SetUp() override {
    logger = std::make_unique<Logger>();
  }
};

// Static member definition (required!)
std::unique_ptr<Logger> NeworoTest::logger;
```