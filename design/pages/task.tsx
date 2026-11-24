import { slot, type Mode } from "../slots.ts";
import type { TaskFixture } from "../fixtures.ts";
import { Layout } from "./layout.tsx";

// The task page. The form posts a plain `source` field, and that is the whole submission path: the editor
// island upgrades the textarea when JavaScript arrives, and when it does not the same field posts anyway.
// The fallback is not a separate code path — removing the island must leave a working form.



export function Task({ mode, data }: { mode: Mode; data: TaskFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.title}
      body={
        <>
          <h1 className="text-2xl font-semibold tracking-tight">
            {slot(mode, "task.title", data.title)}
          </h1>
          <p className="mt-1 text-sm opacity-70">
            {slot(mode, "task.limit_label", data.limit_label)} {slot(mode, "task.time_ms", String(data.time_ms))} ms
            {" · "}
            {slot(mode, "task.used_memory_label", data.used_memory_label)}{" "}
            {slot(mode, "task.memory_used_mb", String(data.memory_used_mb))} MB{" · "}
            {slot(mode, "task.reserved_memory_label", data.reserved_memory_label)}{" "}
            {slot(mode, "task.memory_reserved_mb", String(data.memory_reserved_mb))} MB
          </p>

          <article className="prose mt-6 max-w-none">
            <div dangerouslySetInnerHTML={{ __html: slot(mode, "task.statement", data.statement) }} />
          </article>

          <section className="mt-6">
            <h2 className="text-lg font-semibold">{slot(mode, "example_label", data.example_label)}</h2>
            <pre className="mt-2 overflow-x-auto rounded border border-base-300 bg-base-200 p-3 text-sm">
              {slot(mode, "task.example_input", data.example_input)}
            </pre>
            <pre className="mt-2 overflow-x-auto rounded border border-base-300 bg-base-200 p-3 text-sm">
              {slot(mode, "task.example_output", data.example_output)}
            </pre>
          </section>

          <form
            className="mt-8"
            method="post"
            action={slot(mode, "submit_action", "/tasks/" + data.task_id + "/submit")}
          >
            <label className="mb-2 block text-sm font-medium" htmlFor="source">
              {slot(mode, "source_label", data.source_label)}
            </label>
            <textarea
              id="source"
              name="source"
              rows={16}
              className="textarea textarea-bordered w-full font-mono text-sm"
              spellCheck={false}
              defaultValue={slot(mode, "task.starter_source", data.starter_source)}
            />
            {/* The island mounts here when its script loads. It replaces the textarea's presentation, never
                the field: the name and value stay on the form. */}
            <div data-island="editor" data-field="source" hidden={mode === "template" ? undefined : true} />
            <button className="btn btn-primary mt-3" type="submit">
              {slot(mode, "submit_label", data.submit_label)}
            </button>
          </form>

          {/* Outside the form: the submission must never depend on the island loading. */}
          <script src="/static/island.js" defer />

          {/* One stream per open page: the server pushes the fragment when the verdict write happens, and
              without JavaScript the page simply keeps the pending line it rendered. */}
          <div
            id="verdict-panel"
            data-stream={slot(mode, "verdict_stream_url", "/tasks/" + data.task_id + "/verdict/stream")}
          >
            <section className="mt-8" id="verdict">
              <p className="opacity-70">{slot(mode, "verdict_pending", data.verdict_pending)}</p>
            </section>
          </div>
          <script src="/static/verdict.js" defer />
        </>
      }
    />
  );
}
