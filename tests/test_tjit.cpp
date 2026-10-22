// SPDX-License-Identifier: Apache-2.0
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

#include "tjit/tjit.hpp"

static int failures = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) {                                                          \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

using namespace tjit;
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

static std::unique_ptr<Node> must_parse(const std::string& s) {
  auto r = parse(s);
  if (r.error) {
    std::fprintf(stderr, "parse error in %s at %zu: %s\n", s.c_str(), r.error->offset, r.error->message.c_str());
    ++failures;
    return nullptr;
  }
  return std::move(r.root);
}

static void test_goldens() {
  struct G {
    const char* src;
    bool folded;
    const char* hex;
  } gs[] = {
      {"x", false, "48 89 f8 c3"},
      {"y", false, "48 89 f0 c3"},
      {"z", false, "48 89 d0 c3"},
      {"0", false, "31 c0 c3"},
      {"42", false, "48 c7 c0 2a 00 00 00 c3"},
      {"-1", false, "48 c7 c0 ff ff ff ff c3"},
      {"4294967296", false, "48 b8 00 00 00 00 01 00 00 00 c3"},
      {"x+1", false, "48 89 f8 48 c7 c1 01 00 00 00 48 01 c8 c3"},
      {"x-y", false, "48 89 f8 48 89 f1 48 29 c8 c3"},
      {"x*z", false, "48 89 f8 48 89 d1 48 0f af c1 c3"},
      {"-x", false, "48 89 f8 48 f7 d8 c3"},
      // non-leaf right operand: push/pop around it
      {"x*(y+1)", false, "48 89 f8 50 48 89 f0 48 c7 c1 01 00 00 00 48 01 c8 48 89 c1 58 48 0f af c1 c3"},
      // folding: (2*3)+x*1-0 -> 6 + x
      {"(2*3)+x*1-0", true, "48 c7 c0 06 00 00 00 48 89 f9 48 01 c8 c3"},
      {"x*0+7", true, "48 c7 c0 07 00 00 00 c3"},
      {"--x", true, "48 89 f8 c3"},
  };
  for (const auto& g : gs) {
    auto n = must_parse(g.src);
    if (!n) continue;
    if (g.folded) n = fold(std::move(n));
    const std::string got = hex(emit(*n));
    if (got != g.hex) {
      std::fprintf(stderr, "golden %s:\n  got  %s\n  want %s\n", g.src, got.c_str(), g.hex);
      ++failures;
    }
  }
}

static void test_parse_errors() {
  struct E {
    const char* src;
    std::size_t offset;
  } es[] = {{"", 0}, {"x +", 3}, {"(x", 2}, {"x)", 1}, {"2 3", 2}, {"a", 0}, {"9223372036854775808", 0}, {"x * * y", 4}};
  for (const auto& e : es) {
    auto r = parse(e.src);
    CHECK(r.error.has_value());
    if (r.error && r.error->offset != e.offset) {
      std::fprintf(stderr, "error offset for '%s': got %zu want %zu (%s)\n", e.src, r.error->offset, e.offset,
                   r.error->message.c_str());
      ++failures;
    }
  }
  // limits: INT64_MIN literal is accepted, deep nesting and huge inputs rejected
  auto m = must_parse("-9223372036854775808");
  CHECK(m && m->op == Op::Const && m->value == kMin);
  CHECK(must_parse("9223372036854775807")->value == kMax);
  CHECK(parse(std::string(500, '(') + "x" + std::string(500, ')')).error.has_value());
  CHECK(parse(std::string(500, '-') + "x").error.has_value());
  std::string big = "x";
  for (int i = 0; i < 20000; ++i) big += "+1";
  CHECK(parse(big).error.has_value());
  std::string ok = "x";
  for (int i = 0; i < 4000; ++i) ok += "+1";
  CHECK(!parse(ok).error.has_value());
}

static void test_interpreter_semantics() {
  auto n = must_parse("x*y - z + 3");
  CHECK(interpret(*n, 5, 6, 7) == 26);
  auto w = must_parse("x + 1");
  CHECK(interpret(*w, kMax, 0, 0) == kMin);  // wraps, no UB
  auto ng = must_parse("-x");
  CHECK(interpret(*ng, kMin, 0, 0) == kMin);
}

// Random expression text over x,y,z and interesting constants.
static std::string random_expr(std::mt19937_64& rng, int depth) {
  static const char* consts[] = {"0", "1", "2", "7", "255", "65536", "2147483647", "2147483648", "4294967296",
                                 "9223372036854775807", "-9223372036854775808", "-1", "-2147483648", "-2147483649"};
  if (depth == 0 || rng() % 4 == 0) {
    switch (rng() % 4) {
      case 0: return "x";
      case 1: return "y";
      case 2: return "z";
      default: return consts[rng() % (sizeof consts / sizeof *consts)];
    }
  }
  switch (rng() % 4) {
    case 0: return "(" + random_expr(rng, depth - 1) + " + " + random_expr(rng, depth - 1) + ")";
    case 1: return "(" + random_expr(rng, depth - 1) + " - " + random_expr(rng, depth - 1) + ")";
    case 2: return "(" + random_expr(rng, depth - 1) + " * " + random_expr(rng, depth - 1) + ")";
    default: return "-(" + random_expr(rng, depth - 1) + ")";
  }
}

static void test_differential() {
  std::mt19937_64 rng(2026);
  const std::int64_t args[] = {0, 1, -1, 2, 3, 1000003, kMax, kMin, 0x123456789ABCDEFll, -77};
  int compared = 0;
  for (int i = 0; i < 3000; ++i) {
    const std::string src = random_expr(rng, 1 + static_cast<int>(rng() % 7));
    auto n = must_parse(src);
    if (!n) continue;
    auto folded = fold(must_parse(src));
    const auto code = emit(*n);
    const auto fcode = emit(*folded);
    CHECK(fcode.size() <= code.size());
    for (int k = 0; k < 4; ++k) {
      const std::int64_t x = args[rng() % 10], y = args[rng() % 10], z = static_cast<std::int64_t>(rng());
      const std::int64_t want = interpret(*n, x, y, z);
      CHECK(interpret(*folded, x, y, z) == want);
      if constexpr (host_can_execute()) {
        static std::string err;
        auto buf = ExecBuffer::create(code, &err);
        auto fbuf = ExecBuffer::create(fcode, &err);
        CHECK(buf && fbuf);
        if (!buf || !fbuf) return;
        const std::int64_t got = buf->call(x, y, z), fgot = fbuf->call(x, y, z);
        if (got != want || fgot != want) {
          std::fprintf(stderr, "mismatch %s (x=%" PRId64 " y=%" PRId64 " z=%" PRId64 "): interp %" PRId64 " jit %" PRId64
                               " folded-jit %" PRId64 "\n",
                       src.c_str(), x, y, z, want, got, fgot);
          ++failures;
        }
        ++compared;
      }
    }
  }
  if constexpr (host_can_execute()) {
    CHECK(compared == 12000);
    std::printf("differential: %d evaluations compared (interpreter vs JIT vs folded JIT)\n", compared);
  } else {
    std::printf("differential: not an x86-64 host, JIT execution skipped; folding checked\n");
  }
}

// Finds the /proc/self/maps line that covers `p` and returns its permission field.
static std::string perms_of(const void* p) {
  std::ifstream maps("/proc/self/maps");
  std::string line;
  const auto a = reinterpret_cast<std::uintptr_t>(p);
  while (std::getline(maps, line)) {
    std::uintptr_t lo = 0, hi = 0;
    char perms[8] = {};
    if (std::sscanf(line.c_str(), "%" SCNxPTR "-%" SCNxPTR " %7s", &lo, &hi, perms) == 3 && a >= lo && a < hi)
      return perms;
  }
  return "";
}

static void test_wx_and_ownership() {
  auto n = must_parse("x+y");
  auto buf = ExecBuffer::create(emit(*n));
  CHECK(buf.has_value());
  if (!buf) return;
  const std::string p = perms_of(buf->data());
  CHECK(p.size() == 4 && p[0] == 'r' && p[1] == '-' && p[2] == 'x');  // never writable once executable
  const auto* bytes = static_cast<const std::uint8_t*>(buf->data());
  const std::size_t len = emit(*n).size();
  CHECK(bytes[len] == 0xCC);  // int3 padding after the code
  const void* where = buf->data();
  ExecBuffer moved = std::move(*buf);
  CHECK(moved.data() == where && buf->data() == nullptr);
  if constexpr (host_can_execute()) CHECK(moved.call(40, 2) == 42);
  { ExecBuffer gone = std::move(moved); }
  CHECK(perms_of(where).empty());  // unmapped on destruction
  CHECK(!ExecBuffer::create({}).has_value());
}

int main() {
  test_goldens();
  test_parse_errors();
  test_interpreter_semantics();
  test_differential();
  test_wx_and_ownership();
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
