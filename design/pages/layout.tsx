import { endWhen, slot, when, type Mode } from "../slots.ts";
import { RepeatList } from "../repeat.tsx";

// The shell every page shares: one stylesheet generated from web/tokens.css, the site name, the navigation
// and the language switch. It takes the body as children so a page owns only its own content.
export interface LayoutProps {
  mode: Mode;
  site_name: string;
  heading: string;
  body: React.ReactNode;
}

/// One rung, as the meter draws it: a segment in the bar and a row in the list behind it.
interface RungChoice {
  id: string;
  title: string;
  band: string;
  value: string;
  is_current: boolean;
}

// The gallery's rungs. The template mode emits one row inside the loop the model fills, so the design and
// the page cannot disagree about the shape; only the words come from the fixture.
const meterRungs: RungChoice[] = [
  { id: "toi-zero", title: "TOI-Zero", band: "800–1400", value: "800-1400", is_current: false },
  { id: "camp-1", title: "Camp 1", band: "1500–1800", value: "1500-1800", is_current: true },
  { id: "camp-2", title: "Camp 2", band: "1900–2400", value: "1900-2400", is_current: false },
  { id: "toi-ioi", title: "TOI-IOI", band: "2500–3500", value: "2500-3500", is_current: false },
];

const placeholderRung: RungChoice = {
  id: "r",
  title: "",
  band: "",
  value: "",
  is_current: false,
};

export function Layout({ mode, site_name, heading, body }: LayoutProps) {
  return (
    <html lang={slot(mode, "locale", "th")} data-theme="cms">
      <head>
        <meta charSet="utf-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1" />
        <title>{site_name}</title>
        <link rel="stylesheet" href="/static/app.css" />
        <link rel="icon" href="/static/favicon.svg" type="image/svg+xml" />
        {/* Pinned: htmx 4.0.0, vendored under web/public so production fetches nothing at runtime. The
            page works without it; what it adds is partial refresh, not the submission itself. */}
        <script src="/static/htmx.min.js" defer />
      </head>
      <body className="min-h-dvh bg-base-100 text-base-content">
        <header className="navbar mx-auto max-w-5xl gap-4 border-b border-base-300 px-4">
          <a className="font-semibold tracking-tight" href="/">
            {site_name}
          </a>
          <nav className="ms-auto flex items-center gap-3 text-sm">
            <a className="link link-hover" href="/">
              {slot(mode, "nav.curriculum", "หลักสูตร")}
            </a>
            <a className="link link-hover" href="/tasks">
              {slot(mode, "nav.tasks", "แบบฝึกหัดทั้งหมด")}
            </a>
            <a className="btn btn-sm btn-primary" href="/signin">
              {slot(mode, "nav.sign_in", "เข้าสู่ระบบ")}
            </a>
            {when(mode, "has_level_meter")}
            <details className="dropdown dropdown-end">
              <summary className="btn btn-ghost btn-sm gap-2" title={slot(mode, "nav.level", "ระดับ")}>
                <RepeatList
                  mode={mode}
                  tag="span"
                  className="flex items-end gap-0.5"
                  alias="rung"
                  collection="levels"
                  items={meterRungs}
                  placeholder={placeholderRung}
                >
                  {(rung) => (
                    <span className="flex h-4 w-1.5 items-end rounded-sm border border-base-content/20">
                      {when(mode, "rung.is_current")}
                      <span className="h-full w-full rounded-sm bg-primary" />
                      {endWhen(mode)}
                    </span>
                  )}
                </RepeatList>
                <span className="text-xs opacity-70">
                  {when(mode, "has_level_name")}
                  {slot(mode, "level_name", "ค่าย 1")}
                  {endWhen(mode)}
                  {when(mode, "not has_level_name")}
                  {slot(mode, "any_level_label", "ทุกระดับ")}
                  {endWhen(mode)}
                </span>
              </summary>
              <RepeatList
                mode={mode}
                tag="ul"
                className="dropdown-content menu z-10 mt-2 w-52 rounded-box bg-base-200 p-2 text-sm"
                alias="rung"
                collection="levels"
                items={meterRungs}
                placeholder={placeholderRung}
              >
                {(rung) => (
                  <li>
                    <form method="post" action="/level">
                      <input type="hidden" name="level" value={slot(mode, "rung.id", rung.id)} />
                      <button className="btn btn-ghost btn-sm w-full justify-between" type="submit">
                        <span>{slot(mode, "rung.title", rung.title)}</span>
                        <span className="opacity-60">{slot(mode, "rung.band", rung.band)}</span>
                      </button>
                    </form>
                  </li>
                )}
              </RepeatList>
            </details>
            {endWhen(mode)}
            <a
              className="btn btn-ghost btn-sm"
              href={"?" + "lang=" + slot(mode, "other_locale", "en")}
              hrefLang={slot(mode, "other_locale", "en")}
            >
              {slot(mode, "other_locale_label", "English")}
            </a>
          </nav>
        </header>
        <main className="mx-auto max-w-5xl px-4 py-8">{body}</main>
        <footer className="mx-auto max-w-5xl px-4 py-8 text-sm opacity-70">
          {slot(mode, "footer.notice", "เนื้อหาภาษาไทยและอังกฤษอยู่ในหน้าเดียวกัน")}
        </footer>
      </body>
    </html>
  );
}
