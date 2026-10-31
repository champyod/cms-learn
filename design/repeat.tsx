import { renderToStaticMarkup } from "react-dom/server";
import { loopEnd, loopStart, type Mode } from "./slots.ts";

// A list, rendered two ways from one description.
//
// In design mode the fixture rows are mapped into real elements. In template mode the same row is rendered
// once by React, wrapped in `{% for %}` markers, and handed to the list element as its inner markup — the
// markers must not sit inside an extra element, or the loop would emit a wrapper that `<ol>` cannot hold.
export interface RepeatListProps<T> {
  mode: Mode;
  tag: "ol" | "ul";
  className: string;
  alias: string;
  collection: string;
  items: T[];
  placeholder: T;
  children: (item: T) => React.ReactNode;
}

export function RepeatList<T>({
  mode,
  tag,
  className,
  alias,
  collection,
  items,
  placeholder,
  children,
}: RepeatListProps<T>) {
  const Element = tag;

  if (mode === "template") {
    const row = renderToStaticMarkup(<>{children(placeholder)}</>);
    const inner = loopStart(mode, alias, collection) + row + loopEnd(mode);
    return <Element className={className} dangerouslySetInnerHTML={{ __html: inner }} />;
  }

  return (
    <Element className={className}>
      {items.map((item, index) => (
        <Child key={index}>{children(item)}</Child>
      ))}
    </Element>
  );
}

// React requires a key on a repeated child; a fragment carries it without adding an element.
function Child({ children }: { children: React.ReactNode }) {
  return <>{children}</>;
}
