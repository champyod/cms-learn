import type { FalsifyCase } from "./contract.ts";
import { firstLine } from "./text.ts";

export interface CommandResult { status: number; output: string }
export interface Outcome { line: string; ok: boolean }

const WRONG_REASON_OUTPUT_LIMIT = 160;

/** The one line the runner prints for a case, and whether the gate behaved the way the case expects. */
export function verdictFor(testCase: FalsifyCase, result: CommandResult): Outcome {
  if (testCase.expect.status === "zero") {
    return result.status === 0
      ? { line: `behaved       ${testCase.name}`, ok: true }
      : { line: `BROKEN        ${testCase.name} — the gate rejected a valid tree: ${firstLine(result.output)}`, ok: false };
  }
  if (result.status === 0) return { line: `NOT CAUGHT    ${testCase.name} — the gate passed a planted defect`, ok: false };
  const message = testCase.expect.message;
  if (message === undefined) return { line: `caught        ${testCase.name}`, ok: true };
  const ignoreCase = testCase.expect.messageMatch === "case-insensitive";
  const needle = ignoreCase ? message.toLowerCase() : message;
  const haystack = ignoreCase ? result.output.toLowerCase() : result.output;
  if (!haystack.includes(needle)) {
    return { line: `WRONG REASON  ${testCase.name} — expected "${message}", got: ${firstLine(result.output).slice(0, WRONG_REASON_OUTPUT_LIMIT)}`, ok: false };
  }
  return { line: `caught        ${testCase.name} (${message})`, ok: true };
}

/** The closing line that counts the cases which behaved and the cases which did not. */
export function summaryLine(behaved: number, failed: number): string {
  return `\n${behaved} case(s) behaved, ${failed} did not`;
}
