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

// A conditional, emitted as text for the same reason a loop is: the markers must sit inside the element that
// holds the branch, never inside a wrapper of their own, or the generated template gains an element the
// design never had.
export const when = (mode: Mode, condition: string): string =>
  mode === "template" ? `{% if ${condition} %}` : "";

export const otherwise = (mode: Mode): string => (mode === "template" ? "{% else %}" : "");

export const endWhen = (mode: Mode): string => (mode === "template" ? "{% endif %}" : "");
