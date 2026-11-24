import { endWhen, otherwise, slot, when, type Mode } from "../slots.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

// The generation screen (ADR 0062). A teacher writes a brief, and the page says where the job is: each of
// the seven steps is marked from the artefacts the worker already wrote, so the page never invents a
// progress number and cannot disagree with the record.
//
// The state cell and the refusal are separate rows on purpose: a job that failed has both, and a reader
// needs the sentence the step refused with, not only the word "failed".

export interface GenerateFixture {
  site_name: string;
  heading: string;
  lede: string;
  new_heading: string;
  task_id_label: string;
  task_version_label: string;
  brief_label: string;
  submit_label: string;
  jobs_heading: string;
  empty_label: string;
  refusal_label: string;
  has_jobs: boolean;
  csrf_token: string;
  action: string;
  jobs: GenerationJobRow[];
}

export const generateFixture: GenerateFixture = {
  site_name: "CMS Learn",
  heading: "สร้างโจทย์",
  lede: "งานที่สั่งไว้ และขั้นตอนที่เดินไปแล้ว อ่านจากการบันทึกของแต่ละขั้น ไม่ใช่จากการประมาณ",
  new_heading: "สั่งสร้างงานใหม่",
  task_id_label: "รหัสโจทย์",
  task_version_label: "เวอร์ชัน",
  brief_label: "สิ่งที่ต้องการ",
  submit_label: "สั่งสร้าง",
  jobs_heading: "งานที่สั่งไว้",
  empty_label: "ยังไม่มีงาน",
  refusal_label: "เหตุผลที่หยุด",
  has_jobs: true,
  csrf_token: "csrf",
  action: "/admin/generate",
  jobs: [
    {
      id: "12",
      task_id: "sum-of-values",
      state: "running",
      refusal: "",
      has_refusal: false,
      steps_line: "1 2 3 · 5 6 7",
      steps_done: "3/7",
    },
    {
      id: "11",
      task_id: "graph-walk",
      state: "failed",
      refusal: "step 5: the reference was wrong on the smallest case",
      has_refusal: true,
      steps_line: "1 2 3 4 5 6 7",
      steps_done: "4/7",
    },
  ],
};

const placeholderJob: GenerationJobRow = {
  id: "0",
  task_id: "",
  state: "",
  refusal: "",
  has_refusal: false,
  steps_line: "",
  steps_done: "0/7",
};

export function AdminGenerate({ mode, data }: { mode: Mode; data: GenerateFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.heading}
      body={<Body mode={mode} data={data} />}
    />
  );
}

function Body({ mode, data }: { mode: Mode; data: GenerateFixture }) {
  return (
    <>
      <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "heading", data.heading)}</h1>
      <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>

      <section className="mt-10">
        <h2 className="text-xl font-semibold">{slot(mode, "new_heading", data.new_heading)}</h2>
        <form className="mt-4 grid max-w-sm gap-4" method="post" action={slot(mode, "action", data.action)}>
          <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
          <label className="form-control">
            <span className="label-text">{slot(mode, "task_id_label", data.task_id_label)}</span>
            <input className="input input-bordered w-full" type="text" name="task_id" required />
          </label>
          <label className="form-control">
            <span className="label-text">{slot(mode, "task_version_label", data.task_version_label)}</span>
            <input className="input input-bordered w-full" type="text" name="task_version" required />
          </label>
          <label className="form-control">
            <span className="label-text">{slot(mode, "brief_label", data.brief_label)}</span>
            <textarea className="textarea textarea-bordered w-full" name="brief" rows={4} required />
          </label>
          <button className="btn btn-primary w-fit" type="submit">
            {slot(mode, "submit_label", data.submit_label)}
          </button>
        </form>
      </section>

      <section className="mt-10">
        <h2 className="text-xl font-semibold">{slot(mode, "jobs_heading", data.jobs_heading)}</h2>
        {when(mode, "has_jobs")}
        <RepeatList
          mode={mode}
          tag="div"
          className="mt-6 grid gap-4"
          alias="job"
          collection="jobs"
          items={data.jobs.length > 0 ? data.jobs : [placeholderJob]}
          placeholder={placeholderJob}
        >
          {(job) => (
            <article className="card border border-base-300 bg-base-200">
              <div className="card-body gap-2">
                <header className="flex items-center justify-between gap-3">
                  <span className="font-mono text-sm">{job.task_id}</span>
                  <span className="badge badge-outline">{job.state}</span>
                </header>
                <p className="text-sm opacity-70">
                  {job.steps_done} · <span className="font-mono">{job.steps_line}</span>
                </p>
                {job.has_refusal ? (
                  <p className="text-sm text-error">
                    {data.refusal_label}: {job.refusal}
                  </p>
                ) : null}
              </div>
            </article>
          )}
        </RepeatList>
        {otherwise(mode)}
        <p className="mt-4 opacity-70">{slot(mode, "empty_label", data.empty_label)}</p>
        {endWhen(mode)}
      </section>
    </>
  );
}
