// Reference solution for `range-sum-query`. Used by the verification gate
// (reference-pass + limits-measured); 64-bit sums are required because n reaches 2e5 at 1e9 per
// element, so an int accumulator overflows well before the limits do.
#include <cstdio>
#include <vector>

int main() {
    int n = 0, q = 0;
    if (std::scanf("%d %d", &n, &q) != 2) return 0;

    std::vector<long long> prefix(static_cast<size_t>(n) + 1, 0);
    for (int i = 1; i <= n; ++i) {
        long long value = 0;
        std::scanf("%lld", &value);
        prefix[static_cast<size_t>(i)] = prefix[static_cast<size_t>(i) - 1] + value;
    }

    for (int k = 0; k < q; ++k) {
        int l = 0, r = 0;
        std::scanf("%d %d", &l, &r);
        std::printf("%lld\n", prefix[static_cast<size_t>(r)] - prefix[static_cast<size_t>(l) - 1]);
    }
    return 0;
}
