import { endWhen, otherwise, slot, when, type Mode } from "../slots.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

// The authoring form. Every control on this page comes from a row the database holds: the type picks which
// definitions apply, and the fields are rendered from those definitions, so adding a field is a row and the
// page follows without a rebuild. Nothing here can set a status, a version, an id, a visibility, a seed or an
// actor — those are not fields, and the action refuses a request that names one.

export interface TaskFormFieldRow {
  key: string;
  label: string;
  kind: string;
  is_required: boolean;
  is_text: boolean;
  is_choice: boolean;
  is_integer: boolean;
  is_boolean: boolean;
  hint: string;
  bound_hint: boolean;
  choices: string[];
}

export interface TaskFormTypeRow {
  id: string;
  title: string;
  is_selected: boolean;
}

export interface TaskFormFixture {
  site_name: string;
  heading: string;
  lede: string;
  csrf_token: string;
  notice: string;
  has_notice: boolean;
  message: string;
  has_message: boolean;
  types_label: string;
  no_types_label: string;
  no_fields_label: string;
  title_en_label: string;
  title_th_label: string;
  difficulty_label: string;
  submit_label: string;
  required_label: string;
  types: TaskFormTypeRow[];
  has_types: boolean;
  has_selected: boolean;
  selected_id: string;
  selected_title: string;
  fields: TaskFormFieldRow[];
  has_fields: boolean;
}

export const taskFormFixture: TaskFormFixture = {
  site_name: "CMS Learn",
  heading: "แบบฝึกหัดใหม่",
  lede: "ช่องด้านล่างคือช่องที่ชนิดแบบฝึกหัดนี้ประกาศไว้",
  csrf_token: "0f8c1a5e7b9d4c2a",
  notice: "",
  has_notice: false,
  message: "",
  has_message: false,
  types_label: "ชนิดแบบฝึกหัด",
  no_types_label: "ยังไม่มีชนิดแบบฝึกหัด",
  no_fields_label: "ชนิดนี้ยังไม่ประกาศช่องใด",
  title_en_label: "ชื่อ (อังกฤษ)",
  title_th_label: "ชื่อ (ไทย)",
  difficulty_label: "ระดับความยาก",
  submit_label: "บันทึกฉบับร่าง",
  required_label: "จำเป็น",
  types: [
    { id: "batch", title: "แบบทดสอบชุด", is_selected: true },
  ],
  has_types: true,
  has_selected: true,
  selected_id: "batch",
  selected_title: "แบบทดสอบชุด",
  fields: [
    {
      key: "statement",
      label: "โจทย์",
      kind: "text",
      is_required: true,
      is_text: true,
      is_choice: false,
      is_integer: false,
      is_boolean: false,
      hint: "",
      bound_hint: false,
      choices: [],
    },
    {
      key: "score_mode",
      label: "วิธีให้คะแนน",
      kind: "choice",
      is_required: false,
      is_text: false,
      is_choice: true,
      is_integer: false,
      is_boolean: false,
      hint: "per_case · all_or_nothing · subtasks",
      bound_hint: false,
      choices: ["per_case", "all_or_nothing", "subtasks"],
    },
  ],
  has_fields: true,
};

const placeholderType: TaskFormTypeRow = { id: "", title: "", is_selected: false };

const placeholderField: TaskFormFieldRow = {
  key: "",
  label: "",
  kind: "",
  is_required: false,
  is_text: false,
  is_choice: false,
  is_integer: false,
  is_boolean: false,
  hint: "",
  bound_hint: false,
  choices: [],
};

export function AdminTaskForm({ mode, data }: { mode: Mode; data: TaskFormFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.heading}
      body={
        <>
          <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "heading", data.heading)}</h1>
          <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>
          {when(mode, "has_notice")}
          <p className="mt-4 text-sm text-success">{slot(mode, "notice", data.notice)}</p>
          {endWhen(mode)}
          {when(mode, "has_message")}
          <p className="mt-4 text-sm text-error">{slot(mode, "message", data.message)}</p>
          {endWhen(mode)}

          <h2 className="mt-8 text-xl font-semibold">{slot(mode, "types_label", data.types_label)}</h2>
          {when(mode, "has_types")}
          <RepeatList
            mode={mode}
            tag="div"
            className="mt-2 flex flex-wrap gap-2"
            alias="type"
            collection="types"
            items={data.types}
            placeholder={placeholderType}
          >
            {(type) => (
              <a
                className="btn btn-sm btn-outline"
                href={"/admin/tasks/new?type_id=" + slot(mode, "type.id", type.id)}
              >
                {slot(mode, "type.title", type.title)}
              </a>
            )}
          </RepeatList>
          {otherwise(mode)}
          <p className="mt-2 opacity-70">{slot(mode, "no_types_label", data.no_types_label)}</p>
          {endWhen(mode)}

          {when(mode, "has_selected")}
          <form className="mt-8 grid gap-4" action="/admin/tasks/new" method="post">
            <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
            <input type="hidden" name="type_id" value={slot(mode, "selected_id", data.selected_id)} />
            <h2 className="text-xl font-semibold">{slot(mode, "selected_title", data.selected_title)}</h2>

            <label className="form-control">
              <span className="label-text">{slot(mode, "title_en_label", data.title_en_label)}</span>
              <input className="input input-bordered w-full" type="text" name="title_en" required />
            </label>
            <label className="form-control">
              <span className="label-text">{slot(mode, "title_th_label", data.title_th_label)}</span>
              <input className="input input-bordered w-full" type="text" name="title_th" required />
            </label>
            <label className="form-control">
              <span className="label-text">{slot(mode, "difficulty_label", data.difficulty_label)}</span>
              <input className="input input-bordered w-full" type="text" name="difficulty" required />
            </label>

            {when(mode, "has_fields")}
            <RepeatList
              mode={mode}
              tag="div"
              className="grid gap-4"
              alias="field"
              collection="fields"
              items={data.fields}
              placeholder={placeholderField}
            >
              {(field) => (
                <label className="form-control">
                  <span className="label-text">
                    {slot(mode, "field.label", field.label)}
                    {when(mode, "field.is_required")}
                    <span className="opacity-60"> · {slot(mode, "required_label", data.required_label)}</span>
                    {endWhen(mode)}
                  </span>
                  <input
                    className="input input-bordered w-full"
                    type="text"
                    name={slot(mode, "field.key", field.key)}
                  />
                  {when(mode, "field.is_choice")}
                  <span className="label-text-alt opacity-70">{slot(mode, "field.hint", field.hint)}</span>
                  {endWhen(mode)}
                  {when(mode, "field.bound_hint")}
                  <span className="label-text-alt opacity-70">{slot(mode, "field.hint", field.hint)}</span>
                  {endWhen(mode)}
                </label>
              )}
            </RepeatList>
            {otherwise(mode)}
            <p className="opacity-70">{slot(mode, "no_fields_label", data.no_fields_label)}</p>
            {endWhen(mode)}

            <button className="btn btn-primary w-fit" type="submit">
              {slot(mode, "submit_label", data.submit_label)}
            </button>
          </form>
          {endWhen(mode)}
        </>
      }
    />
  );
}
