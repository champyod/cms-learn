import type { Gate } from "../contract.ts";

export const UPGRADE_GATE: Gate = {
  id: "upgrade",
  stage: ["tools/upgrade.sh", "tools/check-upgrade.sh", "tools/render-stack.ts", "deploy/docker-compose.yml"],
  command: { command: "bash", args: ["tools/check-upgrade.sh"] },
  cases: [
    {
      name: "control",
      expect: { status: "zero" },
    },
    {
      name: "health-gate-dropped",
      plant: [{ kind: "patch", path: "tools/upgrade.sh", find: `healthy "$host" || rollback`, replace: `healthy "$host" || true` }],
      expect: { status: "non-zero", message: "[!!]   health " },
    },
    {
      name: "rollback-does-nothing",
      plant: [{ kind: "patch", path: "tools/upgrade.sh", find: `  docker compose -f "$previous_file" up -d --wait`, replace: "  :" }],
      expect: { status: "non-zero", message: "[!!]   health " },
    },
    {
      name: "switch-rollback-dropped",
      plant: [
        {
          kind: "run",
          script: `sed -i 's#compose up -d --wait || rollback .*#compose up -d --wait || die "x"#' tools/upgrade.sh`,
          verify: `grep -q 'compose up -d --wait || die "x"' tools/upgrade.sh`,
        },
      ],
      expect: { status: "non-zero", message: "[!!]   switch " },
    },
    {
      name: "order-reversed",
      plant: [
        {
          kind: "run",
          script: `sed -i 's#^  pull$#  pull_placeholder#; s#^  migrate$#  pull#; s#^  pull_placeholder$#  migrate#' tools/upgrade.sh`,
          verify: `test "$(grep -n '^  migrate$' tools/upgrade.sh | cut -d: -f1)" -lt "$(grep -n '^  pull$' tools/upgrade.sh | cut -d: -f1)"`,
        },
      ],
      expect: { status: "non-zero", message: "[!!]   healthy " },
    },
    {
      name: "release-not-recorded",
      plant: [{ kind: "patch", path: "tools/upgrade.sh", find: `  cp "$compose_file" "$deployed_file"`, replace: "  :" }],
      expect: { status: "non-zero", message: "[!!]   healthy " },
    },
    {
      name: "migration-failure-ignored",
      plant: [{ kind: "patch", path: "tools/upgrade.sh", find: `|| die "the migration failed; nothing was switched"`, replace: "|| true" }],
      expect: { status: "non-zero", message: "[!!]   schema " },
    },
  ],
};
