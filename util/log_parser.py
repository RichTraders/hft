import re
from collections import defaultdict

# 파일 읽기
with open('/home/neworo/CLionProjects/hft/error.log', 'r') as f:
    lines = f.readlines()

# 타임스탬프 기준으로 이벤트 수집
events_by_time = []

# SLOT_DUMP 파싱용
slot_dumps = {}  # {timestamp: {context, reserved, buy_slots[], sell_slots[]}}

for i, line in enumerate(lines):
    timestamp_match = re.search(r'\[([^\]]+)\]', line)
    if not timestamp_match:
        continue
    timestamp = timestamp_match.group(1)

    # ExecutionReport 파싱 (OrderResult에서 Order Id 추출)
    if '[OrderResult]Order Id:' in line:
        order_match = re.search(r'Order Id:(\d+)', line)
        reserved_match = re.search(r'reserved_position:([-\d.e+-]+)', line)

        if order_match:
            order_id = order_match.group(1)
            reserved = float(reserved_match.group(1)) if reserved_match else None

            # 바로 위 라인에서 ExecutionReport 찾기
            exec_report = None
            if i > 0 and 'ExecutionReport{' in lines[i-1]:
                exec_line = lines[i-1]
                exec_type_match = re.search(r'exec_type=(\w+)', exec_line)
                ord_status_match = re.search(r'ord_status=(\w+)', exec_line)
                side_match = re.search(r'side=(\w+)', exec_line)
                price_match = re.search(r'price=([\d.]+)', exec_line)
                leaves_match = re.search(r'leaves_qty=([\d.]+)', exec_line)
                cum_match = re.search(r'cum_qty=([\d.]+)', exec_line)

                exec_report = {
                    'exec_type': exec_type_match.group(1) if exec_type_match else '?',
                    'ord_status': ord_status_match.group(1) if ord_status_match else '?',
                    'side': side_match.group(1) if side_match else '?',
                    'price': price_match.group(1) if price_match else '?',
                    'leaves_qty': leaves_match.group(1) if leaves_match else '?',
                    'cum_qty': cum_match.group(1) if cum_match else '?',
                }

            if exec_report:
                events_by_time.append({
                    'timestamp': timestamp,
                    'order_id': order_id,
                    'type': 'ExecutionReport',
                    'exec_type': exec_report['exec_type'],
                    'ord_status': exec_report['ord_status'],
                    'side': exec_report['side'],
                    'price': exec_report['price'],
                    'leaves_qty': exec_report['leaves_qty'],
                    'cum_qty': exec_report['cum_qty'],
                    'reserved': reserved
                })

    # Apply[NEW] 파싱
    elif '[Apply][NEW]' in line:
        order_match = re.search(r'order_id=(\d+)', line)
        side_match = re.search(r'side:(\w+)', line)
        layer_match = re.search(r'layer=(\d+)', line)
        reserved_match = re.search(r'reserved_position_=([-\d.e+-]+)', line)

        # OrderRequest에서 가격/수량 찾기
        price = '?'
        qty = '?'
        for j in range(max(0, i-5), i):
            if 'OrderRequest]Sent new order' in lines[j] and order_match and order_match.group(1) in lines[j]:
                price_req = re.search(r'price=([\d.]+)', lines[j])
                qty_req = re.search(r'order_qty=([\d.]+)', lines[j])
                if price_req:
                    price = price_req.group(1)
                if qty_req:
                    qty = qty_req.group(1)
                break

        events_by_time.append({
            'timestamp': timestamp,
            'order_id': order_match.group(1) if order_match else '?',
            'type': 'Apply[NEW]',
            'side': side_match.group(1) if side_match else '?',
            'price': price,
            'qty': qty,
            'layer': layer_match.group(1) if layer_match else '?',
            'reserved': float(reserved_match.group(1)) if reserved_match else None
        })

    # Apply[Replace] 파싱
    elif '[Apply][REPLACE]' in line:
        order_match = re.search(r'order_id=(\d+)', line)
        side_match = re.search(r'side:(\w+)', line)
        layer_match = re.search(r'layer=(\d+)', line)
        reserved_match = re.search(r'reserved_position_=([-\d.e+-]+)', line)

        # OrderRequest에서 가격/수량 찾기
        price = '?'
        qty = '?'
        for j in range(max(0, i-5), i):
            if 'modify order' in lines[j] and order_match and order_match.group(1) in lines[j]:
                price_req = re.search(r'price=([\d.]+)', lines[j])
                qty_req = re.search(r'order_qty=([\d.]+)', lines[j])
                if price_req:
                    price = price_req.group(1)
                if qty_req:
                    qty = qty_req.group(1)
                break

        events_by_time.append({
            'timestamp': timestamp,
            'order_id': order_match.group(1) if order_match else '?',
            'type': 'Apply[REPLACE]',
            'side': side_match.group(1) if side_match else '?',
            'price': price,
            'qty': qty,
            'layer': layer_match.group(1) if layer_match else '?',
            'reserved': float(reserved_match.group(1)) if reserved_match else None
        })

    # TTL Cancel 파싱
    elif '[TTL] Cancel sent' in line:
        order_match = re.search(r'oid=(\d+)', line)
        cancel_match = re.search(r'cancel_id=(\d+)', line)
        layer_match = re.search(r'layer=(\d+)', line)

        events_by_time.append({
            'timestamp': timestamp,
            'order_id': cancel_match.group(1) if cancel_match else '?',
            'type': 'TTL_Cancel',
            'target_order_id': order_match.group(1) if order_match else '?',
            'layer': layer_match.group(1) if layer_match else '?',
        })

    # SLOT_DUMP 파싱
    elif '[SLOT_DUMP] ==========' in line and 'After' in line:
        context_match = re.search(r'========== (.+) ==========', line)
        context = context_match.group(1) if context_match else '?'

        # 다음 라인들에서 정보 수집
        current_side = None
        buy_slots = []
        sell_slots = []
        reserved_value = None

        for j in range(i+1, min(i+50, len(lines))):
            if 'END' in lines[j] and 'SLOT_DUMP' in lines[j]:
                break

            if 'Reserved:' in lines[j]:
                reserved_match = re.search(r'Reserved: ([-\d.]+)', lines[j])
                if reserved_match:
                    reserved_value = float(reserved_match.group(1))

            if '===== BUY Side =====' in lines[j]:
                current_side = 'BUY'
            elif '===== SELL Side =====' in lines[j]:
                current_side = 'SELL'
            elif 'Layer[' in lines[j]:
                layer_match = re.search(r'Layer\[(\d+)\]: state=(\w+), tick=(\d+), price=([\d.]+), qty=([\d.]+), oid=(\d+)', lines[j])
                if layer_match:
                    slot_info = {
                        'layer': layer_match.group(1),
                        'state': layer_match.group(2),
                        'tick': layer_match.group(3),
                        'price': layer_match.group(4),
                        'qty': layer_match.group(5),
                        'oid': layer_match.group(6)
                    }
                    if current_side == 'BUY':
                        buy_slots.append(slot_info)
                    elif current_side == 'SELL':
                        sell_slots.append(slot_info)

        slot_dumps[timestamp] = {
            'context': context,
            'reserved': reserved_value,
            'buy_slots': buy_slots,
            'sell_slots': sell_slots
        }

    # OrderRequest 파싱 (참고용)
    elif 'OrderRequest]Sent' in line and 'order_id=' in line:
        order_match = re.search(r'cl_order_id=(\d+)', line)
        side_match = re.search(r'side=(\w+)', line)
        price_match = re.search(r'price=([\d.]+)', line)
        qty_match = re.search(r'order_qty=([\d.]+)', line)

        req_type = 'NEW' if 'Sent new order' in line else 'MODIFY' if 'modify' in line else 'CANCEL'

        events_by_time.append({
            'timestamp': timestamp,
            'order_id': order_match.group(1) if order_match else '?',
            'type': 'Request',
            'req_type': req_type,
            'side': side_match.group(1) if side_match else '?',
            'price': price_match.group(1) if price_match else '?',
            'qty': qty_match.group(1) if qty_match else '?',
        })

# 타임스탬프 기준으로 정렬
events_by_time.sort(key=lambda x: x['timestamp'])

# 전체 이벤트를 시간 순서대로 출력
print("=" * 140)
print("시간순 주문 흐름 (Order ID별 색상 구분)")
print("=" * 140)

# 주문 ID별 인덱스 할당
order_id_to_idx = {}
current_idx = 1

prev_reserved = None
for event in events_by_time:
    order_id = event['order_id']

    # 새로운 주문 ID가 나타나면 인덱스 할당
    if order_id not in order_id_to_idx:
        order_id_to_idx[order_id] = current_idx
        current_idx += 1

    idx = order_id_to_idx[order_id]
    time = event['timestamp'][-12:]

    if event.get('type') == 'TTL_Cancel':
        target_idx = order_id_to_idx.get(event['target_order_id'], '?')
        print(f"  {time} | [주문 #{idx:2d}] TTL_Cancel (target: 주문 #{target_idx}) | Layer: {event['layer']}")

    elif event.get('type') == 'Request':
        req_type = event['req_type']
        if req_type == 'NEW':
            print(f"  {time} | [주문 #{idx:2d}] Request[NEW]         | {event['side']:4s} @ {event['price']:10s} | Qty: {event['qty']:8s}")
        elif req_type == 'MODIFY':
            print(f"  {time} | [주문 #{idx:2d}] Request[MODIFY]      | {event['side']:4s} @ {event['price']:10s} | Qty: {event['qty']:8s}")
        else:
            print(f"  {time} | [주문 #{idx:2d}] Request[CANCEL]      |")

    elif event['type'] == 'Apply[NEW]':
        print(f"  {time} | [주문 #{idx:2d}] Apply[NEW]           | {event['side']:4s} @ {event['price']:10s} | Qty: {event['qty']:8s} | Layer: {event['layer']}")
        if event['reserved'] is not None:
            if prev_reserved is not None:
                delta = event['reserved'] - prev_reserved
                sign = '+' if delta >= 0 else ''
                print(f"                      └─> Reserved: {prev_reserved:.10f} → {event['reserved']:.10f}  (Δ {sign}{delta:.10f})")
            else:
                print(f"                      └─> Reserved: {event['reserved']:.10f}")
            prev_reserved = event['reserved']

    elif event['type'] == 'Apply[REPLACE]':
        print(f"  {time} | [주문 #{idx:2d}] Apply[REPLACE]       | {event['side']:4s} @ {event['price']:10s} | Qty: {event['qty']:8s} | Layer: {event['layer']}")
        if event['reserved'] is not None:
            if prev_reserved is not None:
                delta = event['reserved'] - prev_reserved
                sign = '+' if delta >= 0 else ''
                print(f"                      └─> Reserved: {prev_reserved:.10f} → {event['reserved']:.10f}  (Δ {sign}{delta:.10f})")
            else:
                print(f"                      └─> Reserved: {event['reserved']:.10f}")
            prev_reserved = event['reserved']

    elif event['type'] == 'ExecutionReport':
        status_str = f"{event['exec_type']:8s} -> {event['ord_status']:12s}"
        print(f"  {time} | [주문 #{idx:2d}] {status_str:25s} | {event['side']:4s} @ {event['price']:10s} | leaves={event['leaves_qty']:8s} cum={event['cum_qty']:8s}")

        if event['reserved'] is not None:
            if prev_reserved is not None:
                delta = event['reserved'] - prev_reserved
                sign = '+' if delta >= 0 else ''
                print(f"                      └─> Reserved: {prev_reserved:.10f} → {event['reserved']:.10f}  (Δ {sign}{delta:.10f})")
            else:
                print(f"                      └─> Reserved: {event['reserved']:.10f}")
            prev_reserved = event['reserved']

        # SLOT_DUMP 정보 출력
        if event['timestamp'] in slot_dumps:
            dump = slot_dumps[event['timestamp']]
            print(f"                      ┌─ SLOT DUMP: {dump['context']}")

            # Calculate expected reserved from slots
            buy_qty_sum = sum(float(slot['qty']) for slot in dump['buy_slots'])
            sell_qty_sum = sum(float(slot['qty']) for slot in dump['sell_slots'])
            expected_reserved = buy_qty_sum - sell_qty_sum

            # BUY slots
            if dump['buy_slots']:
                print(f"                      │  BUY Slots:")
                for slot in dump['buy_slots']:
                    slot_oid_idx = order_id_to_idx.get(slot['oid'], '?')
                    print(f"                      │    L{slot['layer']}: [{slot['state']}] price={slot['price']:>10s} qty={slot['qty']:>8s} (주문 #{slot_oid_idx})")

            # SELL slots
            if dump['sell_slots']:
                print(f"                      │  SELL Slots:")
                for slot in dump['sell_slots']:
                    slot_oid_idx = order_id_to_idx.get(slot['oid'], '?')
                    print(f"                      │    L{slot['layer']}: [{slot['state']}] price={slot['price']:>10s} qty={slot['qty']:>8s} (주문 #{slot_oid_idx})")

            if not dump['buy_slots'] and not dump['sell_slots']:
                print(f"                      │  (All slots empty)")

            # Verify reserved matches slot total
            print(f"                      │  Slot Total: BUY={buy_qty_sum:.10f} - SELL={sell_qty_sum:.10f} = {expected_reserved:.10f}")
            print(f"                      └─ Total Reserved: {dump['reserved']:.10f}", end='')

            if abs(expected_reserved - dump['reserved']) > 1e-9:
                discrepancy = dump['reserved'] - expected_reserved
                print(f"  ⚠️  MISMATCH! Diff={discrepancy:.10f}")
            else:
                print()

print("\n" + "=" * 140)
print(f"총 주문 개수: {len(order_id_to_idx)}")
print("=" * 140)
