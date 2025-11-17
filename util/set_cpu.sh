#!/usr/bin/env bash
# isolate_cpu_1_5.sh
# cgroup v2 + cpuset로 CPU 1-5 전용 그룹 만들기,
# init/system/user(/machine) 최상위 형제에서 1-5 제외 → 전용 그룹을 partition root로 승격
# (SCHED_DEADLINE 허용)
# + (선택) IRQ 1-5에서 제외
#
# 서브커맨드:
#   setup | run "<cmd>" | shell | verify | persist | undo
#   attach <PID> | detach <PID> | cap-check [PID] | grant-bin <PATH>
#   part-root | scope-root [run-XXXX.scope]
#   part-fix | overlap
#   irq-apply | irq-restore | irq-status
#
# 환경변수:
#   CPU_RANGE="1-5"
#   CGROUP_NAME="cpu_1_5"
#   STOP_IRQBALANCE=1

set -euo pipefail
# 글롭 미매칭 안전
shopt -s nullglob

BACKUP_DIR="/var/run/irq-affinity-backup"
BACKUP_FILE_DEFAULT="${BACKUP_DIR}/default_smp_affinity.bak"
BACKUP_FILE_IRQS="${BACKUP_DIR}/irqs.bak"
mkdir -p "${BACKUP_DIR}"

CPU_RANGE="${CPU_RANGE:-0-4}"
CGROUP_NAME="${CGROUP_NAME:-cpu_0_4}"
STOP_IRQBALANCE="${STOP_IRQBALANCE:-1}"

CGROOT="/sys/fs/cgroup"
CGDIR="$CGROOT/$CGROUP_NAME"

# root 보장: root면 0 반환, root가 아니면 sudo exec 시도
# - exec 성공 시: 현재 프로세스가 대체되어 이 함수로 "돌아오지 않음"
# - exec 실패 시: 에러 출력 후 1 반환
need_root() {
  if [[ $EUID -ne 0 ]]; then
    exec sudo -E -- "$0" "$@" || { echo "[FAIL] sudo exec 실패" >&2; return 1; }
  fi
  return 0
}

# 즉시 종료(프로세스 종료). 함수 반환값을 쓰는 패턴이 아니라면 그대로 유지.
die() { echo "ERROR: $*" >&2; exit 1; }

# systemd 존재 여부: command -v의 종료코드를 그대로 사용(0/1)
have_systemd() { command -v systemctl >/dev/null 2>&1; }

# 집합 전개: 유효 토큰(정수 또는 a-b 범위)만 출력
# - 입력이 비어있으면 0 반환(출력 없음)
# - 토큰 중 하나라도 유효하지 않으면 경고 찍고 1 반환
_expand_set() {
  local s="${1:-}"
  [[ -n "$s" ]] || { return 0; }

  local IFS=','; read -ra parts <<< "$s" || { echo "[FAIL] _expand_set: 입력 파싱 실패" >&2; return 1; }

  local p a b i
  for p in "${parts[@]}"; do
    if [[ "$p" =~ ^([0-9]+)-([0-9]+)$ ]]; then
      a="${BASH_REMATCH[1]}"; b="${BASH_REMATCH[2]}"
      # a<=b 검증
      if (( a > b )); then
        echo "[FAIL] _expand_set: 잘못된 범위 '$p' (a>b)" >&2
        return 1
      fi
      for ((i=a; i<=b; i++)); do echo "$i"; done
    elif [[ "$p" =~ ^[0-9]+$ ]]; then
      echo "$p"
    else
      echo "[FAIL] _expand_set: 알 수 없는 토큰 '$p'" >&2
      return 1
    fi
  done

  return 0
}

_compress_set() {
  awk '{a[NR]=$1} END{
    if (NR==0) exit 0; start=a[1]; prev=a[1];
    for(i=2;i<=NR;i++){
      if (a[i]==prev+1){prev=a[i];continue}
      if (start==prev) printf("%s,",start); else printf("%s-%s,",start,prev);
      start=a[i]; prev=a[i];
    }
    if (start==prev) printf("%s\n",start); else printf("%s-%s\n",start,prev);
  }'
}

_subtract_set() {
  local present="$1" remove="$2"

  # 입력 검증
  [[ -n "${present:-}" ]] || { echo "[FAIL] _subtract_set: present 비어있음" >&2; return 1; }
  [[ -n "${remove:-}"  ]] || { echo "[FAIL] _subtract_set: remove 비어있음"  >&2; return 1; }

  # remove 집합 생성
  declare -A rm=()
  while read -r n; do [[ -n "$n" ]] && rm["$n"]=1; done < <(_expand_set "$remove") || {
    echo "[FAIL] _subtract_set: _expand_set(remove) 실패" >&2; return 1;
  }

  # present - remove 계산
  local out
  out="$(
    while read -r n; do [[ -z "${rm[$n]:-}" ]] && echo "$n"; done < <(_expand_set "$present")
  )" || { echo "[FAIL] _subtract_set: 차집합 계산 실패" >&2; return 1; }

  # 압축
  if [[ -n "$out" ]]; then
    local comp
    comp="$(printf "%s\n" "$out" | _compress_set)" || {
      echo "[FAIL] _subtract_set: _compress_set 실패" >&2; return 1;
    }
    printf "%s\n" "$comp"
  else
    # 결과가 비어있을 수 있음(합법). 호출부에서 빈 값 체크.
    echo ""
  fi

  return 0
}


_present_cpus() {
  local f="$CGROOT/cpuset.cpus.effective"
  if [[ ! -f "$f" ]]; then
    echo "[FAIL] cgroup v2/cpuset 미지원: $f 없음" >&2
    return 1
  fi
  cat "$f" 2>/dev/null || { echo "[FAIL] $f 읽기 실패" >&2; return 1; }
  return 0
}

_allowed_cpus_outside_range() {
  local present
  present="$(_present_cpus)" || { echo "[FAIL] _allowed_cpus_outside_range: present 조회 실패" >&2; return 1; }

  local res
  res="$(_subtract_set "$present" "$CPU_RANGE")" || {
    echo "[FAIL] _allowed_cpus_outside_range: 차집합 계산 실패" >&2; return 1;
  }

  # 결과 출력(비어있을 수도 있음; 호출부에서 빈 값 처리)
  printf "%s\n" "$res"
  return 0
}

_has_overlap() {
  local eff="$1" rng="$2"
  [[ -n "${eff:-}" ]] || { echo "[FAIL] _has_overlap: eff 비어있음" >&2; return 1; }
  [[ -n "${rng:-}" ]] || { echo "[FAIL] _has_overlap: rng 비어있음" >&2; return 1; }

  declare -A effm=()
  while read -r n; do [[ -n "$n" ]] && effm["$n"]=1; done < <(_expand_set "$eff") || {
    echo "[FAIL] _has_overlap: _expand_set(eff) 실패" >&2; return 1;
  }

  while read -r m; do
    if [[ -n "$m" ]] && [[ -n "${effm[$m]:-}" ]]; then
      return 0  # overlap 있음
    fi
  done < <(_expand_set "$rng") || {
    echo "[FAIL] _has_overlap: _expand_set(rng) 실패" >&2; return 1;
  }

  return 1  # overlap 없음
}

_enable_cpuset_controller() {
  local ctrl="$CGROOT/cgroup.subtree_control"
    local ctrls="$CGROOT/cgroup.controllers"

    # 이미 켜져 있으면 성공
    if grep -qw cpuset "$ctrl" 2>/dev/null; then
      return 0
    fi

    # cpuset 지원 안 하면 실패
    if ! grep -qw cpuset "$ctrls" 2>/dev/null; then
      echo "루트 컨트롤러에 cpuset 없음: $ctrls" >&2
      return 1
    fi

    # 활성화 시도
    if ! echo +cpuset > "$ctrl" 2>/dev/null; then
      echo "cpuset 컨트롤러 활성화 실패: $ctrl" >&2
      return 1
    fi

    return 0
}

# ---------- partition root ----------
set_partition_root() {
  need_root "$@"
  local path="${1:-$CGDIR}"
  [[ -d "$path" ]] || { echo "$path 없음" >&2; return 1; }

  local file="$path/cpuset.cpus.partition"

  if echo root > "$file" 2>/dev/null; then
    echo "[OK] $path -> partition root"
    return 0
  else
    echo "[WARN] partition root 실패(겹침 가능): $path" >&2
    return 1
  fi
}


_create_isolated_cgroup() {
  mkdir -p "$CGDIR" || return 1

  local mems_parent="$CGROOT/cpuset.mems.effective"
  [[ -f "$mems_parent" ]] || {
    echo "[FAIL] $mems_parent 없음" >&2
    return 1
  }

  cat "$mems_parent" > "$CGDIR/cpuset.mems" || return 1
  echo "$CPU_RANGE" > "$CGDIR/cpuset.cpus" || return 1
  set_partition_root "$CGDIR" || true

  echo -n "[OK] $CGDIR cpuset.cpus.effective = "
  cat "$CGDIR/cpuset.cpus.effective" || return 1

  echo -n "[OK] $CGDIR cpuset.cpus.partition = "
  cat "$CGDIR/cpuset.cpus.partition" 2>/dev/null || echo "(없음)"

  return 0
}

# 1) 겹치면 직접 cpuset.cpus에 allow 써주고, 실패 시 1 반환
_write_cpuset_direct_if_needed() {
  local p="$1" allow="$2"
  local eff
  if ! eff="$(cat "$p/cpuset.cpus.effective" 2>/dev/null)"; then
    # 읽을 수 없으면 건너뜀(실패로 보지 않음)
    return 0
  fi

  # 겹치지 않으면 할 일 없음
  _has_overlap "$eff" "$CPU_RANGE" || return 0

  # 겹치면 써보고 실패 시 1
  if echo "$allow" > "$p/cpuset.cpus" 2>/dev/null; then
    return 0
  else
    echo "[FAIL] $p/cpuset.cpus 쓰기 실패(allow='$allow')" >&2
    return 1
  fi
}

# 2) 최상위 슬라이스 제한: (이미 대부분 반환 처리 있어 추가 수정 無)
#   — 루프 내 write 실패 시 return 1, 마지막에 return 0 유지
_restrict_top_slices() {
  have_systemd || { echo "[FAIL] systemd 필요" >&2; return 1; }

  local allow; allow="$(_allowed_cpus_outside_range)"
  [[ -n "$allow" ]] || { echo "[FAIL] 남길 CPU 없음" >&2; return 1; }

  local mems_parent="$CGROOT/cpuset.mems.effective"
  local mems
  mems="$(cat "$mems_parent" 2>/dev/null)" || { echo "[FAIL] $mems_parent 읽기 실패" >&2; return 1; }

  echo "[INFO] 최상위 형제에서 ${CPU_RANGE} 제외 → 허용=$allow, mems=$mems"

  systemctl set-property --runtime init.scope     AllowedCPUs="$allow" AllowedMemoryNodes="$mems" || return 1
  systemctl set-property --runtime system.slice   AllowedCPUs="$allow" AllowedMemoryNodes="$mems" || return 1
  systemctl set-property --runtime user.slice     AllowedCPUs="$allow" AllowedMemoryNodes="$mems" || return 1
  systemctl set-property --runtime machine.slice  AllowedCPUs="$allow" AllowedMemoryNodes="$mems" 2>/dev/null || true

  for p in "$CGROOT/init.scope" "$CGROOT/system.slice" "$CGROOT/user.slice" "$CGROOT/machine.slice"; do
    [[ -d "$p" ]] || continue
    _write_cpuset_direct_if_needed "$p" "$allow" || return 1
  done

  echo -n "[OK] init.scope   eff: ";   cat "$CGROOT/init.scope/cpuset.cpus.effective"   || return 1
  echo -n "[OK] system.slice eff: ";   cat "$CGROOT/system.slice/cpuset.cpus.effective" || return 1
  echo -n "[OK] user.slice   eff: ";   cat "$CGROOT/user.slice/cpuset.cpus.effective"   || return 1
  [[ -d "$CGROOT/machine.slice" ]] && {
    echo -n "[OK] machine.slice eff: ";
    cat "$CGROOT/machine.slice/cpuset.cpus.effective" || return 1;
  }

  return 0
}

overlap_scan_top() {
  echo "== TOP-LEVEL overlap =="
  local eff part p
  local ret=0

  _print_one() {
    local node="$1"
    local efff="$node/cpuset.cpus.effective"
    local partf="$node/cpuset.cpus.partition"
    local effv="N/A" partv="N/A"

    # effective
    if [[ -e "$efff" ]]; then
      if [[ -r "$efff" ]]; then
        effv="$(cat "$efff" 2>/dev/null || echo -)"
      else
        echo "[WARN] unreadable: $efff" >&2
        ret=1
      fi
    fi

    # partition
    if [[ -e "$partf" ]]; then
      if [[ -r "$partf" ]]; then
        partv="$(cat "$partf" 2>/dev/null || echo -)"
      else
        echo "[WARN] unreadable: $partf" >&2
        ret=1
      fi
    fi

    printf "%-22s eff=%-12s part=%s\n" "${node#/sys/fs/cgroup/}" "$effv" "$partv"
  }

  for p in "$CGROOT" "$CGROOT/init.scope" "$CGROOT/system.slice" "$CGROOT/user.slice" "$CGROOT/machine.slice" "$CGDIR"; do
    [[ -d "$p" ]] || continue
    _print_one "$p"
  done

  # run-*.scope는 레이스가 많으니, 읽는 순간 다시 존재하는지도 확인
  for p in /sys/fs/cgroup/run-*.scope; do
    [[ -d "$p" ]] || continue
    # 사라졌으면 스킵 (실패 카운트 X)
    [[ -d "$p" ]] || continue
    _print_one "$p"
  done

  return $ret
}


part_fix() {
  need_root "$@"                 || return 1
  _restrict_top_slices           || return 1
  set_partition_root "$CGDIR"    || return 1
  overlap_scan_top || true       # ← 진단은 non-fatal
  echo "[OK] part-fix 완료"
  return 0
}

# ---------- CAP/NNP helpers ----------
_cap_bit_set() {
  # $1=hex_mask $2=bit_index
  local hex="${1:-0}" bit="${2:-0}"
  local mask=$((16#$hex))
  (( (mask & (1<<bit)) != 0 ))
}

_cap_check_pid() {
  local pid="${1:-$$}"
  local s="/proc/$pid/status"
  if [[ ! -r "$s" ]]; then
    echo "[FAIL] PID $pid status 읽기 실패: $s" >&2
    return 1
  fi

  local eff bnd amb nnp sec
  eff="$(awk '/^CapEff:/{print $2}' "$s")"  || { echo "[FAIL] CapEff 파싱 실패" >&2; return 1; }
  bnd="$(awk '/^CapBnd:/{print $2}' "$s")"  || true
  amb="$(awk '/^CapAmb:/{print $2}' "$s")"  || true
  nnp="$(awk '/^NoNewPrivs:/{print $2}' "$s")" || true
  sec="$(awk '/^Seccomp:/{print $2}' "$s")" || true

  printf "[CAP] PID=%s CapEff=%s CapBnd=%s CapAmb=%s NoNewPrivs=%s Seccomp=%s\n" \
         "$pid" "$eff" "$bnd" "$amb" "$nnp" "$sec"

  if _cap_bit_set "$eff" 23; then
    echo "[OK] CAP_SYS_NICE(EFF)=1"
  else
    echo "[WARN] CAP_SYS_NICE(EFF)=0  (SCHED_DEADLINE은 실패함)"
  fi
  [[ "${nnp:-1}" == "0" ]] || echo "[WARN] NoNewPrivs=1 → 실행 중 권한 승격 불가 (새 컨텍스트 필요)"
  [[ "${sec:-0}" == "0" ]] || echo "[WARN] Seccomp!=0 → 스케줄 관련 syscall 거부 가능"

  return 0
}

_sets_equal() {  # 범위 표현을 확장해서 동일성 비교
  local A B
  A="$(_expand_set "$1" | sort -n | tr '\n' ',')"
  B="$(_expand_set "$2" | sort -n | tr '\n' ',')"
  [[ "$A" == "$B" ]]
}

_print_ok()   { printf "[OK]  %s\n"   "$*"; }
_print_fail() { printf "[FAIL] %s\n"  "$*" >&2; }
_print_warn() { printf "[WARN] %s\n"  "$*" >&2; }

verify_all() {
  echo "== VERIFY (policy) =="
  local ret=0

  # 0) 기본 파생 값
  local present allow
  if ! present="$(_present_cpus)"; then
    _print_fail "present CPU 조회 실패"
    return 1
  fi
  allow="$(_subtract_set "$present" "$CPU_RANGE")"
  if [[ -z "$allow" ]]; then
    _print_fail "남길 CPU 없음 (present=$present, CPU_RANGE=$CPU_RANGE)"
    return 1
  fi
  echo "[INFO] present=$present, CPU_RANGE=$CPU_RANGE, allow=$allow"

  # 1) 전용 cgroup 검증
  if [[ ! -d "$CGDIR" ]]; then
    _print_fail "전용 cgroup 없음: $CGDIR"
    ret=1
  else
    local cg_eff cg_part
    cg_eff="$(cat "$CGDIR/cpuset.cpus.effective" 2>/dev/null || echo)"
    cg_part="$(cat "$CGDIR/cpuset.cpus.partition" 2>/dev/null || echo)"
    [[ -n "$cg_eff" ]] || { _print_fail "$CGDIR/cpuset.cpus.effective 읽기 실패"; ret=1; }
    if [[ -n "$cg_eff" ]]; then
      if _sets_equal "$cg_eff" "$CPU_RANGE"; then
        _print_ok  "$CGDIR eff == CPU_RANGE ($cg_eff)"
      else
        _print_fail "$CGDIR eff 불일치: eff=$cg_eff, 기대=$CPU_RANGE"
        ret=1
      fi
    fi
    if [[ -n "$cg_part" ]]; then
      if [[ "$cg_part" == root* && "$cg_part" != *invalid* ]]; then
        _print_ok  "$CGDIR partition=$cg_part"
      else
        _print_fail "$CGDIR partition 상태 비정상: $cg_part"
        ret=1
      fi
    else
      _print_fail "$CGDIR/cpuset.cpus.partition 없음/읽기 실패"
      ret=1
    fi
  fi

  # 2) 최상위 형제 검증 (각 slice eff == allow, 그리고 CPU_RANGE와 겹치면 안 됨)
  local slice
  for slice in init.scope system.slice user.slice machine.slice; do
    local dir="$CGROOT/$slice"
    [[ -d "$dir" ]] || { _print_warn "$slice 없음(환경차이)"; continue; }
    local eff; eff="$(cat "$dir/cpuset.cpus.effective" 2>/dev/null || echo)"
    if [[ -z "$eff" ]]; then
      _print_fail "$slice eff 읽기 실패"
      ret=1
      continue
    fi
    if _sets_equal "$eff" "$allow"; then
      _print_ok  "$slice eff == allow ($eff)"
    else
      _print_fail "$slice eff 불일치: eff=$eff, 기대=$allow"
      ret=1
    fi
    if _has_overlap "$eff" "$CPU_RANGE"; then
      _print_fail "$slice eff가 CPU_RANGE와 겹침: eff=$eff, CPU_RANGE=$CPU_RANGE"
      ret=1
    fi
  done

  # 3) 현재 셸이 전용 코어를 쓰고 있지 않은지(일반적으로 기대)
  local ca
  ca="$(grep -m1 Cpus_allowed_list /proc/$$/status 2>/dev/null | awk '{print $2}')" || true
  if [[ -n "$ca" ]]; then
    echo "현재 셸 Cpus_allowed_list: $ca"
    if _has_overlap "$ca" "$CPU_RANGE"; then
      _print_warn "현재 셸이 CPU_RANGE와 겹칩니다 (ca=$ca). 의도된 설정이면 무시."
      # 정책상 반드시 실패는 아님. 필요하다면 ret=1로 강제할 것.
    fi
  else
    _print_warn "현재 셸 Cpus_allowed_list 읽기 실패"
  fi

  [[ $ret -eq 0 ]] && _print_ok "VERIFY 통과" || _print_fail "VERIFY 실패"
  return $ret
}

undo_all() {
  local rc=0

  if have_systemd; then
    systemctl revert init.scope    || rc=1
    systemctl revert system.slice  || rc=1
    systemctl revert user.slice    || rc=1
    systemctl revert machine.slice || rc=1
  fi

  if [[ -d "$CGDIR" ]]; then
    if [[ -s "$CGDIR/cgroup.procs" ]]; then
      echo "[WARN] $CGDIR에 프로세스가 남아 있음. 이동/종료 후 삭제." >&2
      rc=1
    fi
    rmdir "$CGDIR" 2>/dev/null || true
  fi

  echo "[OK] 런타임 설정 원복. (영구 드롭인은 수동 삭제 필요할 수 있음)"
  return $rc
}

# ---------- attach/detach ----------
attach_pid() {
  need_root "$@"; local pid="${1:-}"; [[ -n "$pid" ]] || die "사용법: $0 attach <PID>"
  # 권한/NNP 진단 경고
  _cap_check_pid "$pid" || true
  local nnp; nnp="$(awk '/^NoNewPrivs:/{print $2}' "/proc/$pid/status" 2>/dev/null || echo 1)"
  if [[ "$nnp" != "0" ]]; then
    echo "[WARN] PID $pid 는 NoNewPrivs=1 입니다. 이미 실행 중인 프로세스의 권한은 올릴 수 없습니다."
  fi

  _enable_cpuset_controller
  [[ -d "$CGDIR" ]] || _create_isolated_cgroup
  local mems_parent="$CGROOT/cpuset.mems.effective"
  cat "$mems_parent" > "$CGDIR/cpuset.mems"
  echo "$CPU_RANGE" > "$CGDIR/cpuset.cpus"
  set_partition_root "$CGDIR" || true
  echo "$pid" > "$CGDIR/cgroup.procs" || die "PID 이동 실패"
  echo "[OK] PID $pid -> $CGDIR"
  echo -n "[OK] effective: "; cat "$CGDIR/cpuset.cpus.effective"
}
detach_pid() {
  need_root "$@"; local pid="${1:-}"; [[ -n "$pid" ]] || die "사용법: $0 detach <PID>"
  echo "$pid" > "$CGROOT/cgroup.procs" || die "PID 루트 이동 실패"
  echo "[OK] PID $pid -> $CGROOT"
}

usage() {
  cat <<-USAGE
  사용법:
    sudo $0 setup            # 전용 cgroup 생성 + 형제에서 CPU_RANGE 제외(시도) + partition root 승격 시도
    sudo $0 part-fix         # 겹침 자동해결 + 루트 승격 + 진단
    sudo $0 attach <PID>     # 동작 중 PID를 전용 cgroup으로 편입(권한 승격은 불가)
    sudo $0 detach <PID>     # PID 루트 복귀
    sudo $0 overlap          # 최상위/전용/최근 scope 겹침 상태 출력
    sudo $0 verify           # 상태 출력
    sudo $0 undo             # 런타임 원복(드롭인은 수동 삭제 가능)

USAGE
}

main() {
  local cmd="${1:-}"
  case "$cmd" in
    setup)
      need_root "$@" \
        && _enable_cpuset_controller \
        && _create_isolated_cgroup \
        && _restrict_top_slices
      return $?  # 마지막 실행한 함수의 리턴값을 main의 리턴값으로
      ;;
    part-fix)
      need_root "$@" && part_fix
      return $?
      ;;
    attach)
      shift || true
      attach_pid "$@"
      return $?
      ;;
    detach)
      shift || true
      detach_pid "$@"
      return $?
      ;;
    overlap)
      overlap_scan_top
      return $?
      ;;
    verify)
      verify_all
      return $?
      ;;
    undo)
      need_root "$@" && undo_all
      return $?
      ;;
    ""|-h|--help|help)
      usage
      return 0
      ;;
    *)
      die "알 수 없는 서브커맨드: $cmd"
      ;;
  esac
}

main "$@"
exit $?

