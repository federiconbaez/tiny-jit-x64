// SPDX-License-Identifier: Apache-2.0
#include "tjit/tjit.hpp"

namespace tjit {
namespace {

std::int64_t wrap_add(std::int64_t a, std::int64_t b) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
std::int64_t wrap_sub(std::int64_t a, std::int64_t b) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}
std::int64_t wrap_mul(std::int64_t a, std::int64_t b) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
std::int64_t wrap_neg(std::int64_t a) { return static_cast<std::int64_t>(std::uint64_t{0} - static_cast<std::uint64_t>(a)); }

bool is_const(const Node& n, std::int64_t v) { return n.op == Op::Const && n.value == v; }

}  // namespace

std::int64_t interpret(const Node& n, std::int64_t x, std::int64_t y, std::int64_t z) {
  switch (n.op) {
    case Op::Const:
      return n.value;
    case Op::Param:
      return n.value == 0 ? x : n.value == 1 ? y : z;
    case Op::Add:
      return wrap_add(interpret(*n.lhs, x, y, z), interpret(*n.rhs, x, y, z));
    case Op::Sub:
      return wrap_sub(interpret(*n.lhs, x, y, z), interpret(*n.rhs, x, y, z));
    case Op::Mul:
      return wrap_mul(interpret(*n.lhs, x, y, z), interpret(*n.rhs, x, y, z));
    case Op::Neg:
      return wrap_neg(interpret(*n.lhs, x, y, z));
  }
  return 0;
}

std::size_t node_count(const Node& n) {
  return 1 + (n.lhs ? node_count(*n.lhs) : 0) + (n.rhs ? node_count(*n.rhs) : 0);
}

std::unique_ptr<Node> fold(std::unique_ptr<Node> n) {
  if (n->lhs) n->lhs = fold(std::move(n->lhs));
  if (n->rhs) n->rhs = fold(std::move(n->rhs));
  auto konst = [](std::int64_t v) {
    auto k = std::make_unique<Node>();
    k->op = Op::Const;
    k->value = v;
    return k;
  };
  switch (n->op) {
    case Op::Neg:
      if (n->lhs->op == Op::Const) return konst(wrap_neg(n->lhs->value));
      if (n->lhs->op == Op::Neg) return std::move(n->lhs->lhs);  // --a == a (wrapping)
      break;
    case Op::Add:
    case Op::Sub:
    case Op::Mul: {
      const Node& a = *n->lhs;
      const Node& b = *n->rhs;
      if (a.op == Op::Const && b.op == Op::Const) {
        return konst(n->op == Op::Add   ? wrap_add(a.value, b.value)
                     : n->op == Op::Sub ? wrap_sub(a.value, b.value)
                                        : wrap_mul(a.value, b.value));
      }
      if (n->op == Op::Add && is_const(a, 0)) return std::move(n->rhs);
      if ((n->op == Op::Add || n->op == Op::Sub) && is_const(b, 0)) return std::move(n->lhs);
      if (n->op == Op::Mul) {
        if (is_const(a, 1)) return std::move(n->rhs);
        if (is_const(b, 1)) return std::move(n->lhs);
        // x*0 == 0 is safe: expressions have no side effects.
        if (is_const(a, 0) || is_const(b, 0)) return konst(0);
      }
      break;
    }
    default:
      break;
  }
  return n;
}

}  // namespace tjit
