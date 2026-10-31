import { slot, type Mode } from "../slots.ts";

// The shell every page shares: one stylesheet generated from web/tokens.css, the site name, the navigation
// and the language switch. It takes the body as children so a page owns only its own content.
export interface LayoutProps {
  mode: Mode;
  site_name: string;
  heading: string;
  body: React.ReactNode;
}

export function Layout({ mode, site_name, heading, body }: LayoutProps) {
  return (
    <html lang={slot(mode, "locale", "th")} data-theme="cms">
      <head>
        <meta charSet="utf-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1" />
        <title>{site_name}</title>
        <link rel="stylesheet" href="/static/app.css" />
        <link rel="icon" href="/static/favicon.svg" type="image/svg+xml" />
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
            <a className="link link-hover" href="/submissions">
              {slot(mode, "nav.submissions", "งานที่ส่ง")}
            </a>
            <a className="btn btn-sm btn-primary" href="/signin">
              {slot(mode, "nav.sign_in", "เข้าสู่ระบบ")}
            </a>
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
