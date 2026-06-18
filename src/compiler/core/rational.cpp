#include "rational.hpp"

namespace calliope {
namespace {

std::int64_t igcd(std::int64_t a, std::int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) {
        std::int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

} // namespace

Rational rational(std::int64_t num, std::int64_t den) {
    if (den == 0) return Rational{0, 1};
    if (den < 0) { num = -num; den = -den; }
    std::int64_t g = igcd(num, den);
    if (g > 1) { num /= g; den /= g; }
    return Rational{num, den};
}

Rational rational_from_int(std::int64_t n) {
    return Rational{n, 1};
}

Rational rat_add(Rational a, Rational b) {
    return rational(a.num * b.den + b.num * a.den, a.den * b.den);
}

Rational rat_sub(Rational a, Rational b) {
    return rational(a.num * b.den - b.num * a.den, a.den * b.den);
}

Rational rat_mul(Rational a, Rational b) {
    return rational(a.num * b.num, a.den * b.den);
}

Rational rat_div(Rational a, Rational b) {
    if (b.num == 0) return Rational{0, 1};
    return rational(a.num * b.den, a.den * b.num);
}

Rational rat_neg(Rational a) {
    return Rational{-a.num, a.den};
}

bool rat_eq(Rational a, Rational b) {
    // both are normalized, so component-wise equality suffices
    return a.num == b.num && a.den == b.den;
}

bool rat_lt(Rational a, Rational b) {
    // den > 0 for both, so cross-multiplication preserves direction
    return a.num * b.den < b.num * a.den;
}

} // namespace calliope
