import type { Gate, Plant } from "../contract.ts";

/// The clean fixture both cases start from: a request, the four plan texts, and a bilingual statement that repeats
/// none of them. The leak cases change exactly one file, so a red gate names the surface the plant touched.
const CLEAN: Readonly<Record<string, string>> = {
  "brief.txt": "An array task with questions about ranges of positions.\n",
  "description.txt": "Range sums.\n",
  "plan-approach.txt": "prefix sums\n",
  "plan-complexity.txt": "O(n)\n",
  "plan.txt": '{"approach":"prefix sums","n_max":200000}\n',
  "plan-reason.txt": "the plan fits the declared bounds\n",
  "title.en.txt": "Range Sum Queries\n",
  "title.th.txt": "ผลรวมของช่วงข้อมูล\n",
  "statement.en.txt": "# Range Sum Queries\n\nYou are given n integers and a list of ranges; print the sum of each.\n",
  "statement.th.txt": "# ผลรวมของช่วงข้อมูล\n\nกำหนดจำนวนเต็ม n จำนวน แล้วพิมพ์ผลรวมของช่วงที่ถาม\n",
};

const fixture = (directory: string, overrides: Readonly<Record<string, string>> = {}): Plant[] =>
  Object.entries({ ...CLEAN, ...overrides }).map(([name, content]) => ({
    kind: "write",
    path: `${directory}/${name}`,
    content,
  }));

const command = (directory: string) => ({
  command: "bash",
  args: ["tools/check-brief-leak.sh", directory],
});

/// The brief-leak gate's own fixtures run with no argument; a directory argument compares that fixture, which is
/// what lets a case plant a copied span and watch the gate refuse it (ADR 0035 §4).
export const BRIEF_LEAK_GATE: Gate = {
  id: "brief-leak",
  stage: ["content", "core", "docs/dependencies.md", "generator", "tools"],
  command: { command: "bash", args: ["tools/check-brief-leak.sh"] },
  cases: [
    { name: "self-check", expect: { status: "zero" } },
    {
      name: "clean-control",
      plant: fixture("fixtures/brief-clean"),
      command: command("fixtures/brief-clean"),
      expect: { status: "zero" },
    },
    {
      // The title is the cheapest place to put an answer, and before ADR 0037 neither check read it.
      name: "title-copied-from-the-request",
      plant: fixture("fixtures/brief-title", {
        "title.en.txt": "An array task with questions about ranges of positions\n",
      }),
      command: command("fixtures/brief-title"),
      expect: { status: "non-zero", message: "repeats the request" },
    },
    {
      name: "statement-copied-from-the-request",
      plant: fixture("fixtures/brief-statement", {
        "statement.en.txt":
          "# Range Sum Queries\n\nAn array task with questions about ranges of positions is described below.\n",
      }),
      command: command("fixtures/brief-statement"),
      expect: { status: "non-zero", message: "repeats the request" },
    },
    {
      // Thai has no word spaces, so the word rule cannot see this copy; the character rule is the one that must.
      name: "thai-statement-copied-from-the-request",
      plant: fixture("fixtures/brief-thai", {
        "description.txt": "กำหนดอาร์เรย์ของจำนวนเต็มแล้วตอบคำถามเกี่ยวกับผลรวมของช่วงที่กำหนดให้ทุกคำถาม\n",
        "statement.th.txt":
          "# ผลรวมของช่วงข้อมูล\n\nกำหนดอาร์เรย์ของจำนวนเต็มแล้วตอบคำถามเกี่ยวกับผลรวมของช่วงที่กำหนดให้ทุกคำถาม\n",
      }),
      command: command("fixtures/brief-thai"),
      expect: { status: "non-zero", message: "repeats the request" },
    },
  ],
};
