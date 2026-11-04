import type { Gate } from "./contract.ts";
import type { Outcome } from "./report.ts";
import { evaluateGate } from "./runner.ts";

type Expected = "caught" | "NOT CAUGHT";

interface SelfCheck {
  what: string;
  gate: Gate;
  expected: Expected;
}

const FAKE_GREEN: Gate = {
  id: "self-check-fake-green",
  stage: [],
  command: { command: "bash", args: ["-c", "exit 0"] },
  cases: [
    {
      name: "missed-defect",
      plant: [{ kind: "write", path: "defect.txt", content: "planted\n" }],
      expect: { status: "non-zero", message: "the gate must reject this" },
    },
  ],
};

const FAKE_RED: Gate = {
  id: "self-check-fake-red",
  stage: [],
  command: { command: "bash", args: ["-c", "printf 'the gate rejected the planted defect\\n'; exit 1"] },
  cases: [
    {
      name: "caught-defect",
      plant: [{ kind: "write", path: "defect.txt", content: "planted\n" }],
      expect: { status: "non-zero", message: "the gate rejected the planted defect" },
    },
  ],
};

const CHECKS: readonly SelfCheck[] = [
  { what: "a gate that passes a planted defect is reported NOT CAUGHT", gate: FAKE_GREEN, expected: "NOT CAUGHT" },
  { what: "a gate that rejects the planted defect with its message is reported caught", gate: FAKE_RED, expected: "caught" },
];

const verdictOf = (outcome: Outcome): Expected | "other" => {
  if (outcome.line.startsWith("NOT CAUGHT")) return "NOT CAUGHT";
  if (outcome.line.startsWith("caught")) return "caught";
  return "other";
};

const isHonest = (outcome: Outcome, expected: Expected): boolean =>
  outcome.ok === (expected === "caught") && verdictOf(outcome) === expected;

function main(): number {
  let failed = 0;
  for (const check of CHECKS) {
    const evaluation = evaluateGate(check.gate);
    if (evaluation.failure !== null) {
      console.log(`FAIL  ${check.what} — the run failed: ${evaluation.failure}`);
      failed += 1;
      continue;
    }
    const outcome = evaluation.outcomes.length === 1 ? evaluation.outcomes[0] : undefined;
    if (outcome === undefined) {
      console.log(`FAIL  ${check.what} — got ${evaluation.outcomes.length} case outcome(s), expected exactly 1`);
      failed += 1;
      continue;
    }
    if (isHonest(outcome, check.expected)) {
      console.log(`ok    ${check.what}: ${outcome.line}`);
    } else {
      console.log(`FAIL  ${check.what}: ${outcome.line}`);
      failed += 1;
    }
  }
  console.log(`\n${CHECKS.length - failed} check(s) passed, ${failed} failed`);
  return failed === 0 ? 0 : 1;
}

process.exit(main());
