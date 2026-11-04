/** The message of a thrown value, so one failure prints as one line. */
export const reason = (error: unknown): string => (error instanceof Error ? error.message : String(error));

/** The first non-blank line of command output, or a placeholder when it printed nothing. */
export const firstLine = (output: string): string => output.split("\n").map((line) => line.trim()).find((line) => line !== "") ?? "(no output)";
