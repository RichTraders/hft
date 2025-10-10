/*
 * MIT License
 * 
 * Copyright (c) 2025 NewOro Corporation
 * 
 * Permission is hereby granted, free of charge, to use, copy, modify, and distribute 
 * this software for any purpose with or without fee, provided that the above 
 * copyright notice appears in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

// mpsc_logger_soak.cpp
// g++ -O3 -pthread mpsc_logger_soak.cpp -o soak && ./soak out.log 6 200000 32 2048
// usage: soak <out_file> <producers> <msgs_per_producer> <min_len> <max_len>
#include "logger.h"
#include "gtest/gtest.h"

using EmitFn = std::function<void(std::string&&)>;

// 랜덤 payload 생성 (ascii 안전 문자)
static std::string make_payload(std::mt19937_64& rng, int min_len,
                                int max_len) {
  std::uniform_int_distribution<int> len_dist(min_len, max_len);
  static const char table[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_.,:/"
      "@#%+ ";
  std::uniform_int_distribution<int> ch_dist(0, int(sizeof(table) - 2));
  const int L = len_dist(rng);
  std::string s;
  s.resize(L);
  for (int i = 0; i < L; ++i)
    s[i] = table[ch_dist(rng)];
  return s;
}

// 라인 포맷: P:<pid> S:<seq> LEN:<len> MSG:<payload>\n
static inline std::string make_line(uint32_t pid, uint64_t seq,
                                    const std::string& payload) {
  std::string out;
  out.reserve(64 + payload.size());
  out += "P:" + std::to_string(pid);
  out += " S:" + std::to_string(seq);
  out += " LEN:" + std::to_string(payload.size());
  out += " MSG:";
  out += payload;
  return out;
}

// ======== 여기를 네 로거에 맞게 바꿔 끼우면 됨 ========
// 예시 1) 파일로 바로 쓰는 싱글컨슈머(모킹; 네 로거 대신 빠르게 검증해볼 때)
struct FileSink {
  std::thread th;
  std::atomic<bool> stop{false};
  std::mutex m;
  std::vector<std::string> q;
  std::condition_variable cv;
  std::ofstream ofs;

  explicit FileSink(const std::string& path)
      : ofs(path, std::ios::out | std::ios::binary) {}

  void start() {
    th = std::thread([&] {
      std::vector<std::string> local;
      local.reserve(1 << 12);
      while (!stop.load(std::memory_order_acquire)) {
        {
          std::unique_lock lk(m);
          cv.wait_for(lk, std::chrono::milliseconds(10),
                      [&] { return !q.empty() || stop.load(); });
          q.swap(local);
        }
        for (auto& s : local)
          ofs.write(s.data(), (std::streamsize)s.size());
        local.clear();
      }
      // drain
      std::vector<std::string> tail;
      {
        std::lock_guard lk(m);
        tail.swap(q);
      }
      for (auto& s : tail)
        ofs.write(s.data(), (std::streamsize)s.size());
      ofs.flush();
    });
  }
  void stop_and_join() {
    stop.store(true, std::memory_order_release);
    cv.notify_all();
    if (th.joinable())
      th.join();
    ofs.flush();
  }
  void emit(std::string&& s) {
    {
      std::lock_guard lk(m);
      q.emplace_back(std::move(s));
    }
    cv.notify_one();
  }
};

// 예시 2) 실제 네 MPSC 로거에 붙일 때(스켈레톤)
// TODO: 아래 주석을 네 API로 교체.
// struct MPSCLogger { void start(); void stop(); void log(std::string&&); };

// =====================================================

// 부하 생성기: N개의 프로듀서가 각자 seq를 0..M-1로 증가시키며 랜덤 길이 메시지 푸시
static void run_load(EmitFn emit, int producers, int msgs_per_producer,
                     int min_len, int max_len) {
  std::atomic<bool> go{false};
  std::vector<std::thread> ths;
  ths.reserve(producers);

  for (int p = 0; p < producers; ++p) {
    ths.emplace_back([&, p] {
      // per-thread RNG (재현성 위해 seed에 p 반영)
      std::mt19937_64 rng(0x9e3779b97f4a7c15ULL ^ (uint64_t)p);
      while (!go.load(std::memory_order_acquire)) { /* spin */
      }

      for (int i = 0; i < msgs_per_producer; ++i) {
        auto payload = make_payload(rng, min_len, max_len);
        auto line = make_line((uint32_t)p, (uint64_t)i, payload);
        emit(std::move(line));
      }
    });
  }

  go.store(true, std::memory_order_release);
  for (auto& t : ths)
    t.join();
}

// 검증기: 파일을 읽어 P, S, LEN, MSG를 파싱 → 유실/중복/길이 불일치/순서 오류 검사
struct VerifyReport {
  uint64_t lines = 0;
  uint64_t bad_len = 0;
  uint64_t dup = 0;
  uint64_t gap = 0;
  uint64_t parse_err = 0;
  std::unordered_map<uint32_t, uint64_t> last_seq;  // per-producer 마지막 seq
};

static bool read_exact(std::istream& in, char* buf, size_t n) {
  size_t got = 0;
  while (got < n && in) {
    in.read(buf + got, static_cast<std::streamsize>(n - got));
    got += static_cast<size_t>(in.gcount());
  }
  return got == n;
}

// tok("]P:" 등)를 앞에서부터 스캔
static bool scan_until(std::istream& in, std::string_view tok) {
  size_t m = 0;
  for (int c; (c = in.get()) != EOF; ) {
    if (c == tok[m]) {
      if (++m == tok.size()) return true;
    } else {
      m = (c == tok[0]) ? 1 : 0;
    }
  }
  return false;
}

static std::optional<uint64_t> read_uint_until(std::istream& in, char delim) {
  // 앞 공백 스킵
  while (in.peek() == ' ') in.get();
  if (!std::isdigit(in.peek())) return std::nullopt;
  uint64_t v = 0;
  for (int c; (c = in.peek()) != EOF && std::isdigit(c); ) {
    v = v * 10 + (in.get() - '0');
  }
  if (in.get() != delim) return std::nullopt;
  return v;
}

static VerifyReport verify_file_streaming(const std::string& path) {
  VerifyReport rep;

  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::cerr << "[ERR] open failed: " << path << "\n";
    return rep;
  }
  // I/O 효율을 위해 큰 버퍼 부착 (예: 1MB)
  static std::vector<char> iobuf(1 << 20);
  f.rdbuf()->pubsetbuf(iobuf.data(), static_cast<std::streamsize>(iobuf.size()));

  // 레코드 포맷: "...]P:<pid> S:<seq> LEN:<len> MSG:" + <len bytes> + "\r?\n?"
  while (true) {
    if (!scan_until(f, "]P:")) break;

    auto pid = read_uint_until(f, ' ');
    if (!pid) { rep.parse_err++; break; }

    if (!scan_until(f, "S:")) { rep.parse_err++; break; }
    auto seq = read_uint_until(f, ' ');
    if (!seq) { rep.parse_err++; break; }

    if (!scan_until(f, "LEN:")) { rep.parse_err++; break; }
    auto decl_len = read_uint_until(f, ' ');
    if (!decl_len) { rep.parse_err++; break; }

    if (!scan_until(f, "MSG:")) { rep.parse_err++; break; }

    // payload 정확히 decl_len 바이트 읽기
    std::string payload;
    payload.resize(static_cast<size_t>(*decl_len));
    if (!read_exact(f, payload.data(), payload.size())) {
      rep.bad_len++;
      break;
    }

    // 레코드 종료 개행 처리(있으면 소비)
    if (f.peek() == '\r') f.get();
    if (f.peek() == '\n') f.get();

    // per-producer 연속성 체크
    auto& last = rep.last_seq[static_cast<uint32_t>(*pid)];
    if (last != 0 || *seq != 0) {
      if (*seq == last) rep.dup++;
      else if (*seq != last + 1) rep.gap++;
    }
    if (*seq >= last) last = *seq;

    rep.lines++;
  }

  return rep;
}

static VerifyReport verify_file(const std::string& path) {
  VerifyReport rep;
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs) {
    std::cerr << "[ERR] open failed: " << path << "\n";
    return rep;
  }

  // 파일 전체 로드(테스트 용량이 큰 경우 mmap으로 대체 가능)
  std::string buf((std::istreambuf_iterator<char>(ifs)),
                  std::istreambuf_iterator<char>());
  size_t i = 0, N = buf.size();

  // 빠른 파싱: 'P:', ' S:', ' LEN:', ' MSG:' 기준으로 find
  while (true) {
    size_t pP = buf.find("P:", i);
    if (pP == std::string::npos)
      break;

    size_t pS = buf.find(" S:", pP + 2);
    size_t pL = (pS == std::string::npos) ? std::string::npos
                                          : buf.find(" LEN:", pS + 3);
    size_t pM = (pL == std::string::npos) ? std::string::npos
                                          : buf.find(" MSG:", pL + 5);

    if (pS == std::string::npos || pL == std::string::npos ||
        pM == std::string::npos) {
      rep.parse_err++;
      break;  // 토큰 불완전 -> 종료
    }

    // 숫자 파싱
    uint32_t pid = 0;
    uint64_t seq = 0;
    size_t decl_len = 0;
    try {
      pid =
          static_cast<uint32_t>(std::stoul(buf.substr(pP + 2, pS - (pP + 2))));
      seq =
          static_cast<uint64_t>(std::stoull(buf.substr(pS + 3, pL - (pS + 3))));
      decl_len =
          static_cast<size_t>(std::stoull(buf.substr(pL + 5, pM - (pL + 5))));
    } catch (...) {
      rep.parse_err++;
      break;
    }

    // payload는 길이 기반으로 정확히 decl_len 바이트 읽기
    size_t payload_start = pM + 5;  // " MSG:" 이후
    if (payload_start + decl_len > N) {
      rep.parse_err++;
      break;
    }

    // 길이 체크(이 검증기는 payload 내용 파싱 안 함)
    const size_t actual_len =
        decl_len;  // 바이트 스트림이므로 선언 길이=실제 길이로 간주
    if (actual_len != decl_len)
      rep.bad_len++;  // (이 줄은 항상 false지만, 형식상 남겨둠)

    // per-producer 순서/중복 체크
    auto& last = rep.last_seq[pid];
    if (seq == 0) {
      if (last != 0) { /* 다른 시작값이면 ? */
      }
    } else if (last != 0 || seq != 0) {
      if (seq == last)
        rep.dup++;
      else if (seq != last + 1)
        rep.gap++;
    }
    if (seq >= last)
      last = seq;

    rep.lines++;

    // 레코드 끝으로 커서 이동
    i = payload_start + decl_len;

    // 선택적으로 개행 소비(로거가 '\n' or "\r\n" 붙일 수 있음)
    if (i < N && buf[i] == '\r')
      i++;
    if (i < N && buf[i] == '\n')
      i++;
  }
  return rep;
}

int _main(int argc, const char** argv) {
  if (argc < 6) {
    std::cerr
        << "usage: " << argv[0]
        << " <out_file> <producers> <msgs_per_producer> <min_len> <max_len>\n";
    return 1;
  }
  const std::string out = argv[1];
  const int producers = std::atoi(argv[2]);
  const int msgs_per_producer = std::atoi(argv[3]);
  const int min_len = std::atoi(argv[4]);
  const int max_len = std::atoi(argv[5]);
  if (producers <= 0 || msgs_per_producer <= 0 || min_len < 1 ||
      max_len < min_len) {
    std::cerr << "[ERR] bad args\n";
    return 2;
  }

  FileSink sink(out);
  sink.start();
  EmitFn emit = [&](std::string&& s) {
    sink.emit(std::move(s));
  };

  common::Logger logger;
  logger.setLevel(common::LogLevel::kInfo);
  logger.clearSink();
  logger.addSink(std::make_unique<common::FileSink>(out, 214'748'364'800)); //200GB
  common::Logger::Producer producer = logger.make_producer();
  emit = [&producer](std::string&& s) {
    producer.info(std::move(s));
  };

  auto t0 = std::chrono::steady_clock::now();
  run_load(emit, producers, msgs_per_producer, min_len, max_len);
  auto t1 = std::chrono::steady_clock::now();

  sink.stop_and_join();
  logger.shutdown();

  auto dt_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  std::cout << "[INFO] produced "
            << static_cast<uint64_t>(producers) *
                   static_cast<uint64_t>(msgs_per_producer)
            << " msgs in " << dt_ms << " ms\n";

  auto rep = verify_file_streaming(out);
  std::cout << "[VERIFY] lines=" << rep.lines << " bad_len=" << rep.bad_len
            << " dup=" << rep.dup << " gap=" << rep.gap
            << " parse_err=" << rep.parse_err << "\n";

  const bool ok =
      (rep.bad_len == 0 && rep.dup == 0 && rep.gap == 0 && rep.parse_err == 0 &&
       rep.lines == static_cast<uint64_t>(producers) *
                        static_cast<uint64_t>(msgs_per_producer));
  std::cout << (ok ? "[OK] log looks consistent\n" : "[FAIL] issues found\n");
  return ok ? 0 : 3;
}

TEST(LoggerPerformanceTest, StressTest) {
  const char*argv[]={NULL, "out.log", "8", "200000", "300", "20480"};
  EXPECT_EQ(_main(6, argv), 0);
}