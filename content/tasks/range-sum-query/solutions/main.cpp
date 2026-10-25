// Reference solution for `range-sum-query`. Used by the verification gate (reference-pass +
// limits-measured).
//
// 64-bit sums are required because n reaches 2e5 at 1e9 per element, so an int accumulator
// overflows well before the declared limits do. Every scanf result is checked: a reference that
// silently continues on a parse failure reports "wrong answer" for input it never read — and glibc
// marks scanf warn_unused_result, so ignoring it is a build error under -Werror.
#include <cstdio>
#include <vector>

int main() {
    int n = 0, q = 0;
    if (std::scanf("%d %d", &n, &q) != 2) return 0;

    std::vector<long long> prefix(static_cast<size_t>(n) + 1, 0);
    for (int i = 1; i <= n; ++i) {
        long long value = 0;
        if (std::scanf("%lld", &value) != 1) return 0;
        prefix[static_cast<size_t>(i)] = prefix[static_cast<size_t>(i) - 1] + value;
    }

    for (int k = 0; k < q; ++k) {
        int l = 0, r = 0;
        if (std::scanf("%d %d", &l, &r) != 2) return 0;
        std::printf("%lld\n", prefix[static_cast<size_t>(r)] - prefix[static_cast<size_t>(l) - 1]);
    }
    return 0;
}
