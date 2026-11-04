import type { Gate } from "./contract.ts";
import { PM_GATE } from "./cases/pm.ts";
import { REFERENCES_GATE } from "./cases/references.ts";

/** Every gate the runner knows, in the order it reports them. */
export const GATES: readonly Gate[] = [PM_GATE, REFERENCES_GATE];

/** The gate registered under `id`; throws naming every known id when there is none. */
export function gateById(id: string): Gate {
  const gate = GATES.find((entry) => entry.id === id);
  if (gate === undefined) {
    throw new Error(`unknown gate "${id}"; known gates: ${GATES.map((entry) => entry.id).join(", ")}`);
  }
  return gate;
}
