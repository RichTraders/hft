# Test Data Directory

This directory contains FIX message samples for performance and integration testing.

## Directory Structure

```
test/data/
├── market_data/          # FIX Market Data messages
│   ├── snapshot_*.fix    # MarketDataSnapshotFullRefresh (35=W)
│   └── incremental_*.fix # MarketDataIncrementalRefresh (35=X)
├── execution_reports/    # FIX Order Entry messages
│   ├── new_ack_*.fix     # ExecutionReport - New Order Ack (35=8, ExecType=0)
│   ├── fill_*.fix        # ExecutionReport - Fill (35=8, ExecType=2)
│   └── cancel_*.fix      # ExecutionReport - Cancel (35=8, ExecType=4)
└── README.md             # This file
```

## FIX Message Format

Messages should be stored in raw FIX format (one message per line):

```
8=FIX.4.4|9=XXX|35=W|...|10=XXX|
```

Where `|` represents the FIX field delimiter (SOH, ASCII 0x01).

## How to Add Test Data

### Option 1: Capture from Testnet
```bash
# Run the application with logging enabled
./hft --config test/resources/config.ini --capture-fix

# Extract messages from logs
grep "35=W" logs/fix_messages.log > test/data/market_data/snapshot_001.fix
grep "35=X" logs/fix_messages.log > test/data/market_data/incremental_001.fix
```

### Option 2: Manual Creation
Create FIX messages manually following the FIX 4.4 specification:
https://www.fixtrading.org/standards/fix-4-4/

### Option 3: Use Provided Samples
Sample messages will be provided in each subdirectory.

## Data Usage in Tests

```cpp
// test/nfr_pf_performance_tests.cpp
#include "test/utils/fix_message_loader.h"

TEST(Performance, MarketDataLatency) {
  FixMessageLoader loader;
  auto messages = loader.load_messages("test/data/market_data/incremental_burst.fix");

  for (const auto& msg : messages) {
    // Process message...
  }
}
```

## Important Notes

- **Do NOT commit real API keys or sensitive data**
- Use testnet data only
- Sanitize all PII (Personally Identifiable Information)
- FIX messages are text-based but use SOH (0x01) delimiter
- Message checksums (field 10) must be valid

## Performance Test Data Requirements

For NFR-PF performance tests, we need:

1. **NFR-PF-001 (Latency)**:
   - 10,000+ incremental update messages
   - Varied price levels and quantities
   - Mix of add/modify/delete operations

2. **NFR-PF-002 (Throughput)**:
   - 12 hours worth of market data (~1-10 million messages)
   - Representative of production load
   - Can be synthetic data with realistic patterns

3. **NFR-PF-003 (CPU Affinity)**:
   - Same dataset as NFR-PF-001
   - Tested with/without CPU pinning

## Contact

For questions about test data format or generation, contact the development team.
