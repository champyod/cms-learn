import { renderToStaticMarkup } from "react-dom/server";
import { settingsFixture } from "./catalogue.ts";
import { homeFixture, taskFixture } from "./fixtures.ts";
import { Home } from "./pages/home.tsx";
import { Settings } from "./pages/settings.tsx";
import { Task } from "./pages/task.tsx";
import type { Mode } from "./slots.ts";

// The registry is the single list of pages. The gate compares it against the templates on disk, so a page
// without a generated template — or a template with no page — is a failure rather than a silent omission.
export const pages: Record<string, (mode: Mode) => React.ReactElement> = {
  home: (mode) => <Home mode={mode} data={homeFixture} />,
  settings: (mode) => <Settings mode={mode} data={settingsFixture} />,
  task: (mode) => <Task mode={mode} data={taskFixture} />,
};

export const render = (name: string, mode: Mode): string => {
  const page = pages[name];
  if (page === undefined) {
    throw new Error(`no such page: ${name}`);
  }
  return `<!doctype html>\n${renderToStaticMarkup(page(mode))}\n`;
};
