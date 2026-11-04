import type { Gate } from "../contract.ts";

const ONES_DIGEST = "sha256:1111111111111111111111111111111111111111111111111111111111111111";

export const STACK_GATE: Gate = {
  id: "stack",
  stage: ["release.toml", "deploy"],
  command: { command: "bun", args: ["run", "{repo}/tools/render-stack.ts", "--check", "--root", "{root}"] },
  cases: [
    {
      name: "control",
      expect: { status: "zero" },
    },
    {
      name: "digest-drift",
      plant: [
        {
          kind: "run",
          script: `sed -i 's/^digest = "sha256:979bcc.*/digest = "${ONES_DIGEST}"/' release.toml`,
          verify: `grep -q '${ONES_DIGEST}' release.toml`,
        },
      ],
      expect: { status: "non-zero", message: "is not what release.toml produces" },
    },
    {
      name: "hand-edited-compose",
      plant: [{ kind: "patch", path: "deploy/docker-compose.yml", find: 'max-size: "10m"', replace: 'max-size: "12m"' }],
      expect: { status: "non-zero", message: "is not what release.toml produces" },
    },
    {
      name: "service-needs-missing-image",
      plant: [{ kind: "patch", path: "release.toml", find: "[images.grafana]", replace: "[images.grafana-renamed]" }],
      expect: { status: "non-zero", message: "absent from release.toml" },
    },
    {
      name: "image-nobody-uses",
      plant: [{ kind: "append", path: "release.toml", content: `\n[images.spare]\nkind = "unpublished"\nref = "example/spare"\n` }],
      expect: { status: "non-zero", message: "declared but no service uses it" },
    },
    {
      name: "pinned-without-digest",
      plant: [
        {
          kind: "run",
          script: "sed -i 's/^digest = \"sha256:fa3302.*/digest = \"sha256:notadigest\"/' release.toml",
          verify: "grep -q 'sha256:notadigest' release.toml",
        },
      ],
      expect: { status: "non-zero", message: "a pinned image needs sha256" },
    },
    {
      name: "built-context-missing",
      plant: [{ kind: "patch", path: "release.toml", find: 'context = "proxy"', replace: 'context = "proxy-that-is-not-here"' }],
      expect: { status: "non-zero", message: "has no Dockerfile at" },
    },
  ],
};
