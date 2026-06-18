#include "test.hpp"

#include "rational.hpp"

using namespace calliope;

namespace {
bool is(Rational r, long long n, long long d) { return r.num == n && r.den == d; }
} // namespace

void run_rational_tests() {
    // normalization
    CHECK(is(rational(2, 4), 1, 2));
    CHECK(is(rational(1, -2), -1, 2));   // sign moves to numerator
    CHECK(is(rational(0, 5), 0, 1));
    CHECK(is(rational(6, 3), 2, 1));
    CHECK(is(rational(-4, -8), 1, 2));

    // arithmetic
    CHECK(is(rat_add(rational(1, 4), rational(1, 8)), 3, 8));
    CHECK(is(rat_sub(rational(1, 2), rational(1, 3)), 1, 6));
    CHECK(is(rat_mul(rational(1, 2), rational(2, 3)), 1, 3));
    CHECK(is(rat_div(rational(1, 2), rational(1, 4)), 2, 1));
    CHECK(is(rat_neg(rational(1, 2)), -1, 2));
    CHECK(is(rat_div(rational(1, 2), rational(0, 1)), 0, 1)); // div by zero -> 0

    // comparison
    CHECK(rat_eq(rational(2, 4), rational(1, 2)));
    CHECK(rat_lt(rational(1, 3), rational(1, 2)));
    CHECK(!rat_lt(rational(1, 2), rational(1, 2)));
    CHECK(rat_lt(rational(-1, 2), rational(1, 3)));
}
