// SPDX-License-Identifier: Apache-2.0
// Evaluates one expression over many inputs: tree interpreter, JIT, folded JIT
// and the same expression compiled by the C++ compiler. ns per call, median of 7.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "tjit/tjit.hpp"

static volatile std::int64_t sink;

template <class F>
static double ns_per_call(F f, int n) {
  std::vector<double> runs;
  for (int r = 0; r < 7; ++r) {
    std::int64_t acc = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) acc += f(i, i ^ 0x55, 3);
    auto t1 = std::chrono::steady_clock::now();
    sink = acc;
    runs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / n);
  }
  std::sort(runs.begin(), runs.end());
  return runs[3];
}

int main() {
  const char* src = "(x*x*3 + y*(x-7) - z*z + 2*(1+4)) * (x - y*2 + 11) - (3*3 - 9)*x";
  auto tree = tjit::parse(src).root;
  auto folded = tjit::fold(tjit::parse(src).root);
  const auto code = tjit::emit(*tree), fcode = tjit::emit(*folded);
  std::printf("expr: %s\nnodes %zu -> %zu after folding; code %zu -> %zu bytes\n", src, tjit::node_count(*tree),
              tjit::node_count(*folded), code.size(), fcode.size());
  const int n = 5'000'000;
  std::printf("interpreter      %7.2f ns/call\n",
              ns_per_call([&](std::int64_t x, std::int64_t y, std::int64_t z) { return tjit::interpret(*tree, x, y, z); }, n));
  if constexpr (tjit::host_can_execute()) {
    auto b = tjit::ExecBuffer::create(code), fb = tjit::ExecBuffer::create(fcode);
    auto f = [&](std::int64_t x, std::int64_t y, std::int64_t z) { return b->call(x, y, z); };
    auto ff = [&](std::int64_t x, std::int64_t y, std::int64_t z) { return fb->call(x, y, z); };
    std::printf("jit              %7.2f ns/call\n", ns_per_call(f, n));
    std::printf("jit, folded      %7.2f ns/call\n", ns_per_call(ff, n));
    for (std::int64_t x = -3; x < 3; ++x)
      if (f(x, 5, 9) != tjit::interpret(*tree, x, 5, 9)) return 1;
  }
  auto native = [](std::int64_t x, std::int64_t y, std::int64_t z) {
    auto w = [](std::int64_t a) { return static_cast<std::uint64_t>(a); };
    return static_cast<std::int64_t>((w(x) * w(x) * 3 + w(y) * (w(x) - 7) - w(z) * w(z) + 10) * (w(x) - w(y) * 2 + 11) -
                                     0 * w(x));
  };
  std::printf("C++ -O2 (inlined) %6.2f ns/call  (lower bound, not comparable call overhead)\n", ns_per_call(native, n));
  return 0;
}
