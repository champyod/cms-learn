// A slot is the one place a component decides what a value looks like.
//
// The same component tree is rendered twice: once with real fixture text, for the design gallery a person
// looks at, and once with an Inja expression, for the template the engine ships. That is why the port is a
// render rather than a source-to-source transform — there is nothing to parse, so nothing can drift.
export type Mode = "design" | "template";

export const slot = (mode: Mode, expression: string, value: string): string =>
  mode === "template" ? `{{ ${expression} }}` : value;

// A repeatable block: the design mode renders the fixture rows, the template mode wraps them in a loop.
export const loopStart = (mode: Mode, alias: string, collection: string): string =>
  mode === "template" ? `{% for ${alias} in ${collection} %}` : "";

export const loopEnd = (mode: Mode): string => (mode === "template" ? "{% endfor %}" : "");
