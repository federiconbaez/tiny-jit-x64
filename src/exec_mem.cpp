// SPDX-License-Identifier: Apache-2.0
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "tjit/tjit.hpp"

namespace tjit {

std::optional<ExecBuffer> ExecBuffer::create(const std::vector<std::uint8_t>& code, std::string* error) {
  auto fail = [&](const char* what) -> std::optional<ExecBuffer> {
    if (error) *error = std::string(what) + ": " + std::strerror(errno);
    return std::nullopt;
  };
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  const std::size_t size = (code.size() + page - 1) / page * page;
  if (size == 0) {
    errno = EINVAL;
    return fail("empty code");
  }
  void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) return fail("mmap");
  std::memcpy(mem, code.data(), code.size());
  // Pad with int3 so a jump past the end traps instead of running stale bytes.
  std::memset(static_cast<std::uint8_t*>(mem) + code.size(), 0xCC, size - code.size());
  if (mprotect(mem, size, PROT_READ | PROT_EXEC) != 0) {
    const int e = errno;
    munmap(mem, size);
    errno = e;
    return fail("mprotect(PROT_READ|PROT_EXEC)");
  }
  return ExecBuffer(mem, size);
}

ExecBuffer::ExecBuffer(ExecBuffer&& o) noexcept : mem_(o.mem_), size_(o.size_) {
  o.mem_ = nullptr;
  o.size_ = 0;
}

ExecBuffer& ExecBuffer::operator=(ExecBuffer&& o) noexcept {
  if (this != &o) {
    if (mem_) munmap(mem_, size_);
    mem_ = o.mem_;
    size_ = o.size_;
    o.mem_ = nullptr;
    o.size_ = 0;
  }
  return *this;
}

ExecBuffer::~ExecBuffer() {
  if (mem_) munmap(mem_, size_);
}

Fn ExecBuffer::fn() const {
  Fn f;
  static_assert(sizeof f == sizeof mem_);
  std::memcpy(&f, &mem_, sizeof f);  // object pointer -> function pointer, POSIX-sanctioned
  return f;
}

}  // namespace tjit

namespace tjit {

#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
std::int64_t ExecBuffer::call(std::int64_t x, std::int64_t y, std::int64_t z) const {
  return fn()(x, y, z);
}

}  // namespace tjit
