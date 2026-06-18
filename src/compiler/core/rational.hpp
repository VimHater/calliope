#pragma once

#include <cstdint>

// Exact rational arithmetic — the representation of Duration, tuplets, and tempo
// scaling (language_spec.md O11/§14). Plain data + free functions, C-style API.
//
// Invariant for any Rational produced by these functions: den > 0 and
// gcd(|num|, den) == 1. A zero is 0/1. Construct only via rational()/
// rational_from_int() to maintain it.

namespace calliope {

struct Rational {
    std::int64_t num = 0;
    std::int64_t den = 1;
};

Rational rational(std::int64_t num, std::int64_t den); // normalizes
Rational rational_from_int(std::int64_t n);

Rational rat_add(Rational a, Rational b);
Rational rat_sub(Rational a, Rational b);
Rational rat_mul(Rational a, Rational b);
Rational rat_div(Rational a, Rational b); // b == 0 yields 0/1
Rational rat_neg(Rational a);

bool rat_eq(Rational a, Rational b);
bool rat_lt(Rational a, Rational b);

} // namespace calliope
