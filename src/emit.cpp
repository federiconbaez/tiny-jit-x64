// SPDX-License-Identifier: Apache-2.0
// Stack-machine code generation: every subtree leaves its value in rax.
// A binary node evaluates lhs, pushes rax, evaluates rhs into rax, moves it
// to rcx, pops lhs back into rax and combines. Leaves use direct moves, and a
// right operand that is a leaf skips the push/pop entirely.
#include <cstdio>

#include "tjit/tjit.hpp"

namespace tjit {
namespace {

using Code = std::vector<std::uint8_t>;

void bytes(Code& c, std::initializer_list<std::uint8_t> b) { c.insert(c.end(), b); }

bool fits_i32(std::int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

void imm(Code& c, std::uint64_t v, int n) {
  for (int i = 0; i < n; ++i) c.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// mov <reg>, imm  (reg: 0 = rax, 1 = rcx)
void load_const(Code& c, int reg, std::int64_t v) {
  if (v == 0) {
    bytes(c, {0x31, static_cast<std::uint8_t>(reg == 0 ? 0xC0 : 0xC9)});  // xor eax,eax / xor ecx,ecx
  } else if (fits_i32(v)) {
    bytes(c, {0x48, 0xC7, static_cast<std::uint8_t>(reg == 0 ? 0xC0 : 0xC1)});  // mov r64, imm32 (sign-extended)
    imm(c, static_cast<std::uint64_t>(v), 4);
  } else {
    bytes(c, {0x48, static_cast<std::uint8_t>(reg == 0 ? 0xB8 : 0xB9)});  // movabs r64, imm64
    imm(c, static_cast<std::uint64_t>(v), 8);
  }
}

// mov <reg>, rdi/rsi/rdx
void load_param(Code& c, int reg, std::int64_t idx) {
  // ModRM: 11 src dst with opcode 0x89 (mov r/m64, r64); src = rdi(7)/rsi(6)/rdx(2)
  static constexpr std::uint8_t src[3] = {7, 6, 2};
  const auto dst = static_cast<std::uint8_t>(reg == 0 ? 0 : 1);
  bytes(c, {0x48, 0x89, static_cast<std::uint8_t>(0xC0 | (src[idx] << 3) | dst)});
}

bool is_leaf(const Node& n) { return n.op == Op::Const || n.op == Op::Param; }

void load_leaf(Code& c, int reg, const Node& n) {
  if (n.op == Op::Const) {
    load_const(c, reg, n.value);
  } else {
    load_param(c, reg, n.value);
  }
}

void gen(Code& c, const Node& n) {
  switch (n.op) {
    case Op::Const:
    case Op::Param:
      load_leaf(c, 0, n);
      return;
    case Op::Neg:
      gen(c, *n.lhs);
      bytes(c, {0x48, 0xF7, 0xD8});  // neg rax
      return;
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
      gen(c, *n.lhs);
      if (is_leaf(*n.rhs)) {
        load_leaf(c, 1, *n.rhs);  // rhs straight into rcx
      } else {
        bytes(c, {0x50});  // push rax
        gen(c, *n.rhs);
        bytes(c, {0x48, 0x89, 0xC1});  // mov rcx, rax
        bytes(c, {0x58});              // pop rax
      }
      if (n.op == Op::Add) bytes(c, {0x48, 0x01, 0xC8});        // add rax, rcx
      if (n.op == Op::Sub) bytes(c, {0x48, 0x29, 0xC8});        // sub rax, rcx
      if (n.op == Op::Mul) bytes(c, {0x48, 0x0F, 0xAF, 0xC1});  // imul rax, rcx
      return;
  }
}

}  // namespace

std::vector<std::uint8_t> emit(const Node& n) {
  Code c;
  gen(c, n);
  c.push_back(0xC3);  // ret
  return c;
}

std::string hex(const std::vector<std::uint8_t>& code) {
  std::string s;
  char b[4];
  for (std::size_t i = 0; i < code.size(); ++i) {
    std::snprintf(b, sizeof b, "%02x", code[i]);
    if (i) s += ' ';
    s += b;
  }
  return s;
}

}  // namespace tjit
