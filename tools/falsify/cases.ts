import type { Gate } from "./contract.ts";
import { PM_GATE } from "./cases/pm.ts";
import { REFERENCES_GATE } from "./cases/references.ts";
import { SCHEMA_GATE } from "./cases/schema.ts";

/** Every gate the runner knows, in the order it reports them. */
export const GATES: readonly Gate[] = [PM_GATE, REFERENCES_GATE, SCHEMA_GATE];

const assertUniqueIds = (gates: readonly Gate[]): void => {
  const seen = new Set<string>();
  for (const gate of gates) {
    if (seen.has(gate.id)) throw new Error(`duplicate gate id "${gate.id}" in the registry`);
    seen.add(gate.id);
  }
};

assertUniqueIds(GATES);

/** The gate registered under `id`; throws naming every known id when there is none. */
export function gateById(id: string): Gate {
  const gate = GATES.find((entry) => entry.id === id);
  if (gate === undefined) {
    throw new Error(`unknown gate "${id}"; known gates: ${GATES.map((entry) => entry.id).join(", ")}`);
  }
  return gate;
}
