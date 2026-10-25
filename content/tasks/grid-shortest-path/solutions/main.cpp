// Reference solution for `grid-shortest-path`. Visited is marked when a cell is enqueued, not when
// it is dequeued — the invariant the statement's last paragraph is about.
//
// Every input read is checked: on a malformed line the reference must stop rather than search a
// half-built grid. glibc also marks scanf warn_unused_result, so an unchecked read is a build error
// under -Werror — the check is required by both correctness and the gate.
#include <cstdio>
#include <queue>
#include <string>
#include <utility>
#include <vector>

int main() {
    int n = 0, m = 0;
    if (std::scanf("%d %d", &n, &m) != 2) return 0;
    if (n <= 0 || m <= 0 || m > 1000) return 0;

    std::vector<std::string> grid(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        char buffer[1024] = {0};
        if (std::scanf("%1023s", buffer) != 1) return 0;
        grid[static_cast<size_t>(i)] = buffer;
    }

    int sr = 0, sc = 0, tr = 0, tc = 0;
    if (std::scanf("%d %d %d %d", &sr, &sc, &tr, &tc) != 4) return 0;
    --sr; --sc; --tr; --tc;

    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};
    std::vector<std::vector<int>> dist(static_cast<size_t>(n),
                                       std::vector<int>(static_cast<size_t>(m), -1));
    std::queue<std::pair<int, int>> pending;
    dist[static_cast<size_t>(sr)][static_cast<size_t>(sc)] = 0;
    pending.push({sr, sc});

    while (!pending.empty()) {
        const std::pair<int, int> cell = pending.front();
        pending.pop();
        const int r = cell.first;
        const int c = cell.second;
        for (int d = 0; d < 4; ++d) {
            const int nr = r + dr[d];
            const int nc = c + dc[d];
            if (nr < 0 || nr >= n || nc < 0 || nc >= m) continue;
            if (grid[static_cast<size_t>(nr)][static_cast<size_t>(nc)] == '#') continue;
            if (dist[static_cast<size_t>(nr)][static_cast<size_t>(nc)] != -1) continue;
            dist[static_cast<size_t>(nr)][static_cast<size_t>(nc)] =
                dist[static_cast<size_t>(r)][static_cast<size_t>(c)] + 1;
            pending.push({nr, nc});
        }
    }

    std::printf("%d\n", dist[static_cast<size_t>(tr)][static_cast<size_t>(tc)]);
    return 0;
}
