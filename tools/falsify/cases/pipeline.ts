import type { Gate } from "../contract.ts";

/// The generation pipeline's gate: no vendor of ours, a worker that refuses without configuration, and a role
/// that cannot publish or verify. Every case here breaks one of those and expects the gate to say so.
export const PIPELINE_GATE: Gate = {
  id: "pipeline",
  stage: [
    "tools/check-pipeline.sh",
    "tools/falsify/cases/pipeline_claim.sh",
    "generator/src/claim.cpp",
    "tools/apply-plan.sh",
    "tools/pinned-pgschema.sh",
    "tools/postgres-cluster.sh",
    "tools/compare-plan.ts",
    "tools/render-plan.sh",
    "docs/dependencies.md",
    "deploy",
    "generator/src/config.cpp",
  ],
  command: {
    command: "bash",
    args: ["tools/check-pipeline.sh"],
    env: { CMS_GENERATE_BINARY: "{repo}/build/safety/generator/cms_generate" },
  },
  cases: [
    {
      name: "control",
      expect: { status: "zero" },
    },
    {
      // A vendor of ours in the tree: the default endpoint ADR 0028 forbids, whether in code, a config or an
      // example that would silently work.
      name: "vendor-endpoint-in-the-tree",
      plant: [
        {
          kind: "write",
          path: "generator/src/vendor_default.cpp",
          content: "// a default that must never be compiled in\nconst char* kEndpoint = \"https://api.openai.com/v1\";\n",
        },
      ],
      expect: { status: "non-zero", message: "a vendor of ours appears in the tree" },
    },
    {
      // The role gains the grant it must never hold. The plan is forged and its digests rewritten, so the apply
      // accepts it and the refusal the gate asserts is the only thing left to catch the change.
      name: "generator-role-can-update-tasks",
      plant: [
        {
          kind: "run",
          script:
            "printf '\\nGRANT UPDATE ON tasks TO cmslearn_generator;\\n' >> deploy/schema/grants.sql && " +
            "bash tools/render-plan.sh",
          // pgschema renders every grant with ON TABLE, so the witness looks for what the plan really holds.
          verify: "grep -q 'GRANT UPDATE ON TABLE tasks TO cmslearn_generator' deploy/plan/0001_init.sql",
        },
      ],
      expect: { status: "non-zero", message: "the generator role wrote tasks.status" },
    },
    {
      // The queue claim, proven the way the product runs it: the statement is read out of the source and run, so a
      // claim that stops filtering by state and lease hands one job to two workers and this case says so.
      name: "claim-takes-a-job-once",
      command: { command: "bash", args: ["tools/falsify/cases/pipeline_claim.sh"] },
      expect: { status: "zero" },
    },
    {
      // The predicate that makes the queue a queue. Without it every claim takes whatever row is oldest, including
      // one another worker is running.
      name: "claim-drops-the-state-and-lease-predicate",
      plant: [
        {
          kind: "patch",
          path: "generator/src/claim.cpp",
          find:
            "            WHERE state = 'queued'\n" +
            "               OR (state = 'running' AND lease_expires_at < now())\n",
          replace: "",
        },
      ],
      command: { command: "bash", args: ["tools/falsify/cases/pipeline_claim.sh"] },
      expect: { status: "non-zero", message: "handed the same job to a second worker" },
    },
    {
      // The other half of the concurrency story: a row another worker is claiming at this instant is skipped, not
      // waited for and not handed out twice.
      name: "claim-drops-the-locked-row-skip",
      plant: [
        {
          kind: "patch",
          path: "generator/src/claim.cpp",
          find: "FOR UPDATE SKIP LOCKED",
          replace: "FOR UPDATE",
        },
      ],
      command: { command: "bash", args: ["tools/falsify/cases/pipeline_claim.sh"] },
      expect: { status: "non-zero", message: "does not skip a row another worker is claiming" },
    },
    {
      // A worker that starts without the deployer's configuration: the refusal the gate relies on becomes a
      // silent success, which is how a product ends up calling a default nobody chose.
      name: "worker-starts-without-configuration",
      plant: [
        {
          kind: "run",
          script:
            "mkdir -p fakebin && printf '#!/usr/bin/env bash\\nexit 0\\n' > fakebin/cms_generate && " +
            "chmod +x fakebin/cms_generate",
          verify: "test -x fakebin/cms_generate",
        },
      ],
      command: {
        command: "bash",
        args: ["tools/check-pipeline.sh"],
        env: {
          CMS_GENERATE_BINARY: "{root}/fakebin/cms_generate",
        },
      },
      expect: { status: "non-zero", message: "the worker started without configuration" },
    },
  ],
};
