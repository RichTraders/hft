#!/usr/bin/env python3
import argparse, os, re, sys
from configparser import ConfigParser

SRC_EXTS = {".cpp", ".cc", ".cxx", ".hpp", ".hh", ".hxx", ".h"}

# 주석 제거(단순)
RE_BLOCK = re.compile(r"/\*.*?\*/", re.DOTALL)
RE_LINE  = re.compile(r"//.*?$", re.MULTILINE)

# INI_CONFIG.<함수>( ... ) 호출 위치 잡기
RE_CALL_HEAD = re.compile(
    r'INI_CONFIG\s*\.\s*(get|get_int|get_uint64_t|get_double|get_float)\s*\(',
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

def iter_sources(root:str):
    for dp,_,fns in os.walk(root):
        for fn in fns:
            if os.path.splitext(fn)[1].lower() in SRC_EXTS:
                yield os.path.join(dp, fn)

def main():
    ap = argparse.ArgumentParser(description="INI_CONFIG.get* 호출 ↔ INI 정의 검증기")
    ap.add_argument("--ini", required=True)
    ap.add_argument("--root", default=".")
    ap.add_argument("--show-ok", action="store_true")
    ap.add_argument("--dump-ini", action="store_true", help="읽힌 INI 섹션/키를 출력")
    args = ap.parse_args()

    if not os.path.isfile(args.ini):
        print(f"[ERR] INI 파일 없음: {args.ini}", file=sys.stderr); sys.exit(2)

    cfg = load_ini(args.ini)
    if args.dump_ini:
        print("=== INI dump (sections/keys) ===")
        for sec in cfg.sections():
            print(f"[{sec}]")
            for k,_ in cfg.items(sec):
                print(f"  {k}")
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
    print(f"소스: {os.path.abspath(args.root)}")
    print(f"INI : {os.path.abspath(args.ini)}\n")

    missing = 0
    dynamic = 0
    for path, line, func, sec_e, key_e, s_val, k_val in findings:
        loc = f"{path}:{line}"
        if s_val is None or k_val is None:
            print(f"[SKIP] {loc} -> {func}({sec_e.strip()}, {key_e.strip()}) : 동적 인자(문자열 아님)")
            dynamic += 1
            continue
        has_sec = cfg.has_section(s_val)
        has_key = cfg.has_option(s_val, k_val) if has_sec else False
        if has_sec and has_key:
            if args.show_ok:
                print(f"[OK]   {loc} -> [{s_val}] {k_val} 정의됨")
        elif not has_sec:
            print(f"[MISS] {loc} -> 섹션 [{s_val}] 없음 (키 {k_val})")
            missing += 1
        else:
            print(f"[MISS] {loc} -> 섹션 [{s_val}]에 키 '{k_val}' 없음")
            missing += 1

    if not findings:
        print("[INFO] 호출을 하나도 찾지 못했습니다. (확장자/매크로명 확인)")

    print("\n요약:", end=" ")
    if missing: print(f"누락 {missing}건", end="")
    else: print("누락 없음", end="")
    if dynamic: print(f", 동적 인자 {dynamic}건(검사 제외)", end="")
    print(".")
    sys.exit(1 if missing else 0)

if __name__ == "__main__":
    main()
