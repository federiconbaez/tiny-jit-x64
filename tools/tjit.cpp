// SPDX-License-Identifier: Apache-2.0
// usage: tjit [--hex] [--no-fold] EXPR [x [y [z]]]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "tjit/tjit.hpp"

int main(int argc, char** argv) {
  bool show_hex = false, do_fold = true;
  int i = 1;
  for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; ++i) {
    if (!std::strcmp(argv[i], "--hex")) {
      show_hex = true;
    } else if (!std::strcmp(argv[i], "--no-fold")) {
      do_fold = false;
    } else {
      std::fprintf(stderr, "unknown flag %s\n", argv[i]);
      return 2;
    }
  }
  if (i >= argc) {
    std::fprintf(stderr, "usage: tjit [--hex] [--no-fold] EXPR [x [y [z]]]\n");
    return 2;
  }
  const std::string src = argv[i++];
  auto r = tjit::parse(src);
  if (r.error) {
    std::fprintf(stderr, "%s\n%*s^ %s\n", src.c_str(), static_cast<int>(r.error->offset), "", r.error->message.c_str());
    return 1;
  }
  auto tree = do_fold ? tjit::fold(std::move(r.root)) : std::move(r.root);
  std::int64_t a[3] = {0, 0, 0};
  for (int k = 0; k < 3 && i < argc; ++k, ++i) a[k] = std::strtoll(argv[i], nullptr, 0);
  const auto code = tjit::emit(*tree);
  if (show_hex) std::printf("%zu bytes: %s\n", code.size(), tjit::hex(code).c_str());
  if constexpr (tjit::host_can_execute()) {
    std::string err;
    auto buf = tjit::ExecBuffer::create(code, &err);
    if (!buf) {
      std::fprintf(stderr, "cannot map code: %s\n", err.c_str());
      return 1;
    }
    std::printf("%lld\n", static_cast<long long>(buf->call(a[0], a[1], a[2])));
  } else {
    std::printf("%lld (interpreted: host is not x86-64)\n", static_cast<long long>(tjit::interpret(*tree, a[0], a[1], a[2])));
  }
  return 0;
}
