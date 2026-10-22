// SPDX-License-Identifier: Apache-2.0
#include <cctype>
#include <limits>

#include "tjit/tjit.hpp"

namespace tjit {
namespace {

std::unique_ptr<Node> leaf(Op op, std::int64_t v) {
  auto n = std::make_unique<Node>();
  n->op = op;
  n->value = v;
  return n;
}

std::unique_ptr<Node> bin(Op op, std::unique_ptr<Node> a, std::unique_ptr<Node> b) {
  auto n = std::make_unique<Node>();
  n->op = op;
  n->lhs = std::move(a);
  n->rhs = std::move(b);
  return n;
}

class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}

  ParseResult run() {
    auto e = expr();
    skip();
    if (!err_ && pos_ != s_.size()) fail("unexpected character");
    if (!err_ && e && node_count(*e) > kMaxNodes) {
      pos_ = 0;
      fail("expression too large (more than 10000 nodes)");
    }
    if (err_) return {nullptr, err_};
    return {std::move(e), std::nullopt};
  }

 private:
  static constexpr int kMaxDepth = 200;       // bounds parser recursion on hostile input
  static constexpr std::size_t kMaxNodes = 10000;  // bounds every later tree walk
  std::string_view s_;
  std::size_t pos_ = 0;
  int depth_ = 0;
  std::optional<ParseError> err_;

  void fail(const char* m) {
    if (!err_) err_ = ParseError{pos_, m};
  }
  void skip() {
    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
  }
  bool eat(char c) {
    skip();
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  std::size_t ops_ = 0;
  bool budget() {
    if (++ops_ > kMaxNodes) {
      fail("expression too large (more than 10000 nodes)");
      return false;
    }
    return true;
  }

  std::unique_ptr<Node> expr() {
    if (++depth_ > kMaxDepth) {
      fail("expression nested too deeply");
      return nullptr;
    }
    auto n = term();
    while (!err_ && budget()) {
      if (eat('+')) {
        n = bin(Op::Add, std::move(n), term());
      } else if (eat('-')) {
        n = bin(Op::Sub, std::move(n), term());
      } else {
        break;
      }
    }
    --depth_;
    return n;
  }

  std::unique_ptr<Node> term() {
    auto n = unary();
    while (!err_ && eat('*') && budget()) n = bin(Op::Mul, std::move(n), unary());
    return n;
  }

  std::unique_ptr<Node> unary() {
    if (eat('-')) {
      if (++depth_ > kMaxDepth) {
        fail("expression nested too deeply");
        return nullptr;
      }
      // "-9223372036854775808" is a literal, not the negation of an overflowing one.
      skip();
      if (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
        auto lit = number(true);
        --depth_;
        return lit;
      }
      auto inner = unary();
      --depth_;
      auto n = std::make_unique<Node>();
      n->op = Op::Neg;
      n->lhs = std::move(inner);
      return n;
    }
    return atom();
  }

  std::unique_ptr<Node> number(bool negative) {
    // Accumulate as the magnitude in unsigned; allow exactly 2^63 when negative.
    std::uint64_t mag = 0;
    const std::uint64_t limit = negative ? (std::uint64_t{1} << 63) : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::size_t start = pos_;
    while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
      const auto d = static_cast<std::uint64_t>(s_[pos_] - '0');
      if (mag > (limit - d) / 10) {
        pos_ = start;
        fail("integer literal out of range");
        return nullptr;
      }
      mag = mag * 10 + d;
      ++pos_;
    }
    const std::int64_t v = negative ? static_cast<std::int64_t>(std::uint64_t{0} - mag) : static_cast<std::int64_t>(mag);
    return leaf(Op::Const, v);
  }

  std::unique_ptr<Node> atom() {
    skip();
    if (pos_ >= s_.size()) {
      fail("unexpected end of input");
      return nullptr;
    }
    const char c = s_[pos_];
    if (std::isdigit(static_cast<unsigned char>(c))) return number(false);
    if (c == 'x' || c == 'y' || c == 'z') {
      ++pos_;
      return leaf(Op::Param, c - 'x');
    }
    if (eat('(')) {
      auto e = expr();
      if (!err_ && !eat(')')) fail("expected ')'");
      return e;
    }
    fail("expected a number, x, y, z or '('");
    return nullptr;
  }
};

}  // namespace

ParseResult parse(std::string_view src) { return Parser(src).run(); }

}  // namespace tjit
