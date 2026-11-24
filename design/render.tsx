import { renderToStaticMarkup } from "react-dom/server";
import { settingsFixture } from "./catalogue.ts";
import { adminFixture, homeFixture, modelsFixture, signinFixture, signupFixture, taskFixture, taskListFixture } from "./fixtures.ts";
import { AdminHome } from "./pages/admin_home.tsx";
import { AdminGenerate, generateFixture } from "./pages/admin_generate.tsx";
import { AdminModels } from "./pages/admin_models.tsx";
import { AdminTaskForm, taskFormFixture } from "./pages/admin_task_form.tsx";
import { Home } from "./pages/home.tsx";
import { Progress, progressFixture } from "./pages/progress.tsx";
import { Settings } from "./pages/settings.tsx";
import { Signin } from "./pages/signin.tsx";
import { Signup } from "./pages/signup.tsx";
import { Task } from "./pages/task.tsx";
import { Tasks } from "./pages/tasks.tsx";
import type { Mode } from "./slots.ts";

// The registry is the single list of pages. The gate compares it against the templates on disk, so a page
// without a generated template — or a template with no page — is a failure rather than a silent omission.
export const pages: Record<string, (mode: Mode) => React.ReactElement> = {
  admin_home: (mode) => <AdminHome mode={mode} data={adminFixture} />,
  admin_generate: (mode) => <AdminGenerate mode={mode} data={generateFixture} />,
  admin_models: (mode) => <AdminModels mode={mode} data={modelsFixture} />,
  admin_task_form: (mode) => <AdminTaskForm mode={mode} data={taskFormFixture} />,
  home: (mode) => <Home mode={mode} data={homeFixture} />,
  progress: (mode) => <Progress mode={mode} data={progressFixture} />,
  settings: (mode) => <Settings mode={mode} data={settingsFixture} />,
  signin: (mode) => <Signin mode={mode} data={signinFixture} />,
  signup: (mode) => <Signup mode={mode} data={signupFixture} />,
  task: (mode) => <Task mode={mode} data={taskFixture} />,
  tasks: (mode) => <Tasks mode={mode} data={taskListFixture} />,
};

export const render = (name: string, mode: Mode): string => {
  const page = pages[name];
  if (page === undefined) {
    throw new Error(`no such page: ${name}`);
  }
  return `<!doctype html>\n${renderToStaticMarkup(page(mode))}\n`;
};
