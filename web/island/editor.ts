import { defaultKeymap, history, historyKeymap, indentWithTab } from "@codemirror/commands";
import { cpp } from "@codemirror/lang-cpp";
import { bracketMatching, defaultHighlightStyle, indentOnInput, syntaxHighlighting } from "@codemirror/language";
import { EditorState } from "@codemirror/state";
import {
  drawSelection,
  EditorView,
  highlightActiveLine,
  highlightActiveLineGutter,
  keymap,
  lineNumbers,
} from "@codemirror/view";

// Writes every change back into the textarea the form posts: the island is presentation, and a page
// where this script never loads keeps submitting the field it rendered.
const ISLAND_SELECTOR = '[data-island="editor"]';
const MIN_EDITOR_HEIGHT = "21rem";

const EXTENSIONS = [
  lineNumbers(),
  highlightActiveLineGutter(),
  highlightActiveLine(),
  drawSelection(),
  history(),
  indentOnInput(),
  bracketMatching(),
  syntaxHighlighting(defaultHighlightStyle),
  keymap.of([...defaultKeymap, ...historyKeymap, indentWithTab]),
  EditorView.theme({
    "&": { minHeight: MIN_EDITOR_HEIGHT },
    ".cm-scroller": { fontFamily: "var(--font-mono)", overflow: "auto" },
  }),
  cpp(),
];

function findSourceField(container: HTMLElement): HTMLTextAreaElement | null {
  const fieldName = container.dataset.field;
  if (fieldName === undefined) {
    return null;
  }
  const field = document.getElementById(fieldName);
  return field instanceof HTMLTextAreaElement ? field : null;
}

function mount(container: HTMLElement): void {
  const field = findSourceField(container);
  if (field === null) {
    return;
  }
  new EditorView({
    state: EditorState.create({
      doc: field.value,
      extensions: [
        ...EXTENSIONS,
        EditorView.updateListener.of((update) => {
          if (update.docChanged) {
            field.value = update.state.doc.toString();
          }
        }),
      ],
    }),
    parent: container,
  });
  field.hidden = true;
  container.hidden = false;
}

function start(): void {
  const container = document.querySelector<HTMLElement>(ISLAND_SELECTOR);
  if (container !== null) {
    mount(container);
  }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", start, { once: true });
} else {
  start();
}
