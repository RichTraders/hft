#!/usr/bin/env python3
"""
INI Config Checker for HFT project.

Supports the multi-file config structure:
  - resources/config.ini (profile: auth, environment, symbol, strategy)
  - resources/auth/config-{auth}.ini
  - resources/env/config-{environment}.ini
  - resources/symbol/config-{symbol}.ini
  - hft/src/strategy/{strategy}/config-{strategy}.ini

Usage:
  # Check with merged configs (auto-resolve profile)
  ./ini_checker.py --profile resources/config.ini --root hft

  # Check with single INI file (legacy mode)
  ./ini_checker.py --ini test/resources/config-xrpusdc.ini --root hft
"""
import argparse, os, re, sys
from configparser import ConfigParser
from pathlib import Path

SRC_EXTS = {".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx", ".h", ".tpp"}

# 주석 제거(단순)
RE_BLOCK = re.compile(r"/\*.*?\*/", re.DOTALL)
RE_LINE  = re.compile(r"//.*?$", re.MULTILINE)

# INI_CONFIG.<함수>( ... ) 호출 위치 잡기
RE_CALL_HEAD = re.compile(
    r'INI_CONFIG\s*\.\s*(get|get_int|get_int64|get_uint64_t|get_double|get_float|get_string|get_cstring|get_bool)\s*\(',
    re.DOTALL
)

# 문자열 리터럴: 접두사(u8|u|U|L) 허용
RE_STRING = re.compile(
    r'^\s*(?:u8|U|u|L)?("([^"\\]|\\.)*"|\'([^\'\\]|\\.)*\')\s*$',
    re.DOTALL
)

def remove_comments(code:str)->str:
    return RE_LINE.sub("", RE_BLOCK.sub("", code))

def find_calls(code:str):
    """ INI_CONFIG.get*(sec, key, ...) 위치와 전체 괄호 내용을 뽑는다 """
    for m in RE_CALL_HEAD.finditer(code):
        start_paren = m.end()-1  # '(' 위치
        # 괄호 범위 파싱
        i = start_paren
        depth = 0
        in_str = False
        str_quote = ''
        esc = False
        while i < len(code):
            ch = code[i]
            if in_str:
                if esc:
                    esc = False
                elif ch == '\\':
                    esc = True
                elif ch == str_quote:
                    in_str = False
            else:
                if ch in ('"', "'"):
                    in_str = True; str_quote = ch
                elif ch == '(':
                    depth += 1
                elif ch == ')':
                    depth -= 1
                    if depth == 0:
                        # m.group(1)=함수명, args=괄호 안
                        yield (m.group(1), code[start_paren+1:i], m.start())
                        break
            i += 1

def split_top_level_commas(s:str):
    """ 괄호 안 문자열을 최상위 콤마 기준으로 분리 """
    parts = []
    buf = []
    depth = 0
    in_str = False
    str_quote = ''
    esc = False
    for ch in s:
        if in_str:
            buf.append(ch)
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == str_quote:
                in_str = False
        else:
            if ch in ('"', "'"):
                in_str = True; str_quote = ch; buf.append(ch)
            elif ch == '(':
                depth += 1; buf.append(ch)
            elif ch == ')':
                depth -= 1; buf.append(ch)
            elif ch == ',' and depth == 0:
                parts.append(''.join(buf).strip()); buf = []
            else:
                buf.append(ch)
    if buf:
        parts.append(''.join(buf).strip())
    return parts

def str_literal_value(expr:str):
    """ 문자열 리터럴이면 따옴표 제거 후 값 반환, 아니면 None """
    m = RE_STRING.match(expr)
    if not m: return None
    s = expr.strip()
    quote = s[s.find('"') if '"' in s else s.find("'")]
    body = s[s.find(quote)+1 : s.rfind(quote)]
    # 간단한 이스케이프 처리
    return bytes(body, "utf-8").decode("unicode_escape")

def load_ini(path:str)->ConfigParser:
    cfg = ConfigParser()
    cfg.optionxform = str  # 키 소문자화 방지(매우 중요)
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        cfg.read_file(f)
    return cfg

def merge_ini(base: ConfigParser, other: ConfigParser) -> ConfigParser:
    """Merge other into base (other overwrites base)"""
    for sec in other.sections():
        if not base.has_section(sec):
            base.add_section(sec)
        for key, val in other.items(sec):
            base.set(sec, key, val)
    return base

def load_profile_configs(profile_path: str, base_dir: str = None) -> ConfigParser:
    """
    Load and merge all config files based on profile.

    Profile config.ini contains:
      [profile]
      auth = auth (optional)
      environment = prod
      symbol = XRPUSDC
      strategy = directional_strategy

    Loads in order (later overwrites earlier):
      1. resources/auth/config-{auth}.ini (if auth specified)
      2. resources/env/config-{environment}.ini
      3. resources/symbol/config-{symbol}.ini
      4. hft/src/strategy/{strategy}/config-{strategy}.ini
    """
    if base_dir is None:
        base_dir = os.path.dirname(profile_path) or "."

    # Resolve to absolute path
    base_dir = os.path.abspath(base_dir)

    # Load profile first
    profile = load_ini(profile_path)

    # Start with profile config
    merged = ConfigParser()
    merged.optionxform = str
    merge_ini(merged, profile)

    # Get profile values
    auth = profile.get("profile", "auth", fallback=None)
    env = profile.get("profile", "environment", fallback=None)
    symbol = profile.get("profile", "symbol", fallback=None)
    strategy = profile.get("profile", "strategy", fallback=None)

    # List of config files to load
    configs_to_load = []

    # 1. Auth config (optional)
    if auth:
        auth_path = os.path.join(base_dir, "auth", f"config-{auth}.ini")
        if os.path.exists(auth_path):
            configs_to_load.append(("auth", auth_path))

    # 2. Environment config
    if env:
        env_path = os.path.join(base_dir, "env", f"config-{env}.ini")
        if os.path.exists(env_path):
            configs_to_load.append(("env", env_path))

    # 3. Symbol config
    if symbol:
        symbol_path = os.path.join(base_dir, "symbol", f"config-{symbol}.ini")
        if os.path.exists(symbol_path):
            configs_to_load.append(("symbol", symbol_path))

    # 4. Strategy config (look in multiple locations)
    if strategy:
        # Try multiple locations
        strategy_paths = [
            # Production: hft/src/strategy/{strategy}/config-{strategy}.ini
            os.path.join(base_dir, "..", "hft", "src", "strategy", strategy, f"config-{strategy}.ini"),
            # Test: test/resources/strategy/config-{strategy}.ini
            os.path.join(base_dir, "strategy", f"config-{strategy}.ini"),
            # Alternative naming: config-maker.ini for strategy=maker
            os.path.join(base_dir, "strategy", f"config-{strategy}.ini"),
        ]
        for sp in strategy_paths:
            sp = os.path.normpath(sp)
            if os.path.exists(sp):
                configs_to_load.append(("strategy", sp))
                break

    # Load and merge all
    loaded_files = [profile_path]
    for name, path in configs_to_load:
        try:
            cfg = load_ini(path)
            merge_ini(merged, cfg)
            loaded_files.append(path)
        except Exception as e:
            print(f"[WARN] Failed to load {name} config: {path}: {e}", file=sys.stderr)

    return merged, loaded_files

def iter_sources(root:str):
    for dp,_,fns in os.walk(root):
        for fn in fns:
            if os.path.splitext(fn)[1].lower() in SRC_EXTS:
                yield os.path.join(dp, fn)

def main():
    ap = argparse.ArgumentParser(
        description="INI_CONFIG.get* 호출 ↔ INI 정의 검증기",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Check with profile-based merged configs (production setup)
  ./ini_checker.py --profile resources/config.ini --root hft

  # Check with single INI file (legacy/test mode)
  ./ini_checker.py --ini test/resources/config-xrpusdc.ini --root hft

  # Check test resources with profile
  ./ini_checker.py --profile test/resources/config-xrpusdc.ini --root hft
        """
    )
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--ini", help="단일 INI 파일 (legacy mode)")
    group.add_argument("--profile", help="프로필 기반 config.ini (병합 모드)")
    ap.add_argument("--root", default="hft", help="소스 검색 경로 (default: hft)")
    ap.add_argument("--show-ok", action="store_true", help="정상 항목도 출력")
    ap.add_argument("--dump-ini", action="store_true", help="읽힌 INI 섹션/키를 출력")
    ap.add_argument("--show-dynamic", action="store_true", help="동적 인자도 출력")
    args = ap.parse_args()

    # Load config(s)
    loaded_files = []
    if args.ini:
        if not os.path.isfile(args.ini):
            print(f"[ERR] INI 파일 없음: {args.ini}", file=sys.stderr)
            sys.exit(2)
        cfg = load_ini(args.ini)
        loaded_files = [args.ini]
    else:  # --profile
        if not os.path.isfile(args.profile):
            print(f"[ERR] 프로필 파일 없음: {args.profile}", file=sys.stderr)
            sys.exit(2)
        cfg, loaded_files = load_profile_configs(args.profile)

    # Print loaded files
    print("=== 로드된 설정 파일 ===")
    for f in loaded_files:
        print(f"  {os.path.abspath(f)}")
    print()

    if args.dump_ini:
        print("=== INI dump (sections/keys) ===")
        for sec in sorted(cfg.sections()):
            print(f"[{sec}]")
            for k, v in cfg.items(sec):
                # Truncate long values
                v_display = v[:50] + "..." if len(v) > 50 else v
                print(f"  {k} = {v_display}")
        print()

    findings = []
    for path in iter_sources(args.root):
        try:
            raw = open(path, "r", encoding="utf-8", errors="ignore").read()
        except Exception as e:
            print(f"[WARN] 못 읽음: {path}: {e}", file=sys.stderr); continue
        code = remove_comments(raw)
        for func, argstr, pos in find_calls(code):
            line = raw.count("\n", 0, pos) + 1
            args_list = split_top_level_commas(argstr)
            if len(args_list) < 2:
                continue
            sec_expr, key_expr = args_list[0], args_list[1]
            s_val = str_literal_value(sec_expr)
            k_val = str_literal_value(key_expr)
            findings.append((path, line, func, sec_expr, key_expr, s_val, k_val))

    print("=== INI_CONFIG.get* 검사 ===")
    print(f"소스: {os.path.abspath(args.root)}\n")

    missing = 0
    dynamic = 0
    ok_count = 0

    for path, line, func, sec_e, key_e, s_val, k_val in findings:
        # Make path relative for cleaner output
        rel_path = os.path.relpath(path)
        loc = f"{rel_path}:{line}"

        if s_val is None or k_val is None:
            if args.show_dynamic:
                print(f"[SKIP] {loc} -> {func}({sec_e.strip()}, {key_e.strip()}) : 동적 인자")
            dynamic += 1
            continue

        has_sec = cfg.has_section(s_val)
        has_key = cfg.has_option(s_val, k_val) if has_sec else False

        if has_sec and has_key:
            ok_count += 1
            if args.show_ok:
                val = cfg.get(s_val, k_val)
                val_display = val[:30] + "..." if len(val) > 30 else val
                print(f"[OK]   {loc} -> [{s_val}] {k_val} = {val_display}")
        elif not has_sec:
            print(f"[MISS] {loc} -> 섹션 [{s_val}] 없음 (키 {k_val})")
            missing += 1
        else:
            print(f"[MISS] {loc} -> 섹션 [{s_val}]에 키 '{k_val}' 없음")
            missing += 1

    if not findings:
        print("[INFO] 호출을 하나도 찾지 못했습니다. (확장자/매크로명 확인)")

    print(f"\n=== 요약 ===")
    print(f"  정상: {ok_count}건")
    print(f"  누락: {missing}건")
    print(f"  동적 인자(검사 제외): {dynamic}건")

    if missing:
        print(f"\n[FAIL] {missing}건의 누락된 설정이 있습니다.")
        sys.exit(1)
    else:
        print(f"\n[PASS] 모든 설정이 정의되어 있습니다.")
        sys.exit(0)

if __name__ == "__main__":
    main()
