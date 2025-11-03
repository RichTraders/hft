#!/bin/bash

# 대상 호스트와 포트
HOST="fix-md.binance.com"
PORT="9000"

# SOH 문자 (ASCII 0x01)
SOH=$'\x01'

# 현재 UTC 시각 (FIX 표준 형식: YYYYMMDD-HH:MM:SS.sss)
TIMESTAMP=$(date -u +"%Y%m%d-%H:%M:%S.%6N")

# Body 생성 (9= 와 10=은 나중에 추가)
BODY="35=5${SOH}49=BMDWATCH${SOH}56=SPOT${SOH}34=4${SOH}52=$TIMESTAMP"

# Body length 계산 (9= 태그를 제외한 바이트 수)
BODY_LENGTH=${#BODY}

# Header 생성
HEADER="8=FIX.4.4${SOH}9=$BODY_LENGTH"

# 메시지 (체크섬 전)
MSG="${HEADER}${SOH}${BODY}${SOH}"

# 체크섬 계산 (전체 MSG의 바이트 합 % 256)
CHECKSUM=$(echo -n "$MSG" | od -An -t u1 | awk '{ for(i=1;i<=NF;i++) sum+=$i } END { printf "%03d", sum % 256 }')

# 최종 메시지에 10= 체크섬 추가
FINAL_MSG="${MSG}10=${CHECKSUM}${SOH}"

echo "$FINAL_MSG"
# 전송
echo -ne "$FINAL_MSG" | openssl s_client -connect $HOST:$PORT
