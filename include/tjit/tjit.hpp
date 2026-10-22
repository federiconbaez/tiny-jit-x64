// SPDX-License-Identifier: Apache-2.0
// tiny-jit-x64: integer expressions -> x86-64 machine code in W^X memory.
//
// Language: 64-bit two's-complement integers with wrapping arithmetic.
//   expr   := term (('+' | '-') term)*
//   term   := unary ('*' unary)*
//   unary  := '-' unary | atom
//   atom   := INT | 'x' | 'y' | 'z' | '(' expr ')'
// x, y, z are the first three arguments (SysV: rdi, rsi, rdx).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tjit {

enum class Op : std::uint8_t { Const, Param, Add, Sub, Mul, Neg };

struct Node {
  Op op;
  std::int64_t value = 0;  // Const: literal; Param: index 0..2
  std::unique_ptr<Node> lhs, rhs;
};

struct ParseError {
  std::size_t offset;
  std::string message;
};

struct ParseResult {
  std::unique_ptr<Node> root;
  std::optional<ParseError> error;
};

ParseResult parse(std::string_view src);

/// Folds constant subtrees and applies identities (x+0, x*1, x*0, --x).
std::unique_ptr<Node> fold(std::unique_ptr<Node> n);

/// Reference semantics: wrapping 64-bit arithmetic.
std::int64_t interpret(const Node& n, std::int64_t x, std::int64_t y, std::int64_t z);

std::size_t node_count(const Node& n);

/// Emits SysV x86-64 code for `int64_t f(int64_t x, int64_t y, int64_t z)`.
/// Pure function of the tree; usable (and tested) on any host architecture.
std::vector<std::uint8_t> emit(const Node& n);

/// Hex dump "48 89 f8 c3".
std::string hex(const std::vector<std::uint8_t>& code);

using Fn = std::int64_t (*)(std::int64_t, std::int64_t, std::int64_t);

/// A page-aligned executable mapping. Written while PROT_READ|PROT_WRITE, then
/// flipped to PROT_READ|PROT_EXEC; it is never writable and executable at once.
class ExecBuffer {
 public:
  static std::optional<ExecBuffer> create(const std::vector<std::uint8_t>& code, std::string* error = nullptr);
  ExecBuffer(ExecBuffer&& o) noexcept;
  ExecBuffer& operator=(ExecBuffer&& o) noexcept;
  ExecBuffer(const ExecBuffer&) = delete;
  ExecBuffer& operator=(const ExecBuffer&) = delete;
  ~ExecBuffer();

  const void* data() const { return mem_; }
  std::size_t size() const { return size_; }
  Fn fn() const;
  /// Calls the code. Prefer this over fn(): with clang's -fsanitize=function a
  /// raw indirect call reads a type signature in front of the target, which
  /// JIT code does not have (the page before it may not even be mapped).
  std::int64_t call(std::int64_t x, std::int64_t y = 0, std::int64_t z = 0) const;

 private:
  ExecBuffer(void* mem, std::size_t size) : mem_(mem), size_(size) {}
  void* mem_ = nullptr;
  std::size_t size_ = 0;
};

/// True when emitted code can run here (x86-64 host).
constexpr bool host_can_execute() {
#if defined(__x86_64__)
  return true;
#else
  return false;
#endif
}

}  // namespace tjit
