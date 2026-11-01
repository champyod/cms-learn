import { renderToStaticMarkup } from "react-dom/server";
import { endWhen, otherwise, slot, when, type Mode } from "../slots.ts";
import type { SettingsFixture, SettingEntry } from "../catalogue.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

// The settings form, generated from the catalogue rather than written per key: adding a setting is a change
// to deploy/catalogue/settings.json and nothing else.
//
// A secret is the one field with a rule attached. Its value is never rendered — the field arrives empty, a
// change replaces it, and the page says so. The branch that enforces this is emitted into the template, so
// the rule lives in one place and applies to every secret the catalogue declares.

export function Settings({ mode, data }: { mode: Mode; data: SettingsFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.title}
      body={
        <>
          <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "title", data.title)}</h1>
          <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>

          <form className="mt-8 grid gap-4" method="post" action={slot(mode, "action", "/settings")}>
            <RepeatList
              mode={mode}
              tag="ul"
              className="contents"
              alias="setting"
              collection="settings"
              items={data.settings}
              placeholder={placeholderSetting}
            >
              {(setting) => <Field mode={mode} setting={setting} />}
            </RepeatList>
            <button className="btn btn-primary mt-2" type="submit">
              {slot(mode, "submit_label", data.submit_label)}
            </button>
          </form>
        </>
      }
    />
  );
}

const SECRET_HINT = "ปล่อยว่างไว้เพื่อคงค่าเดิม";

/// The row a template-mode render is built from. Its tier is the secret one, so the generated block carries
/// the branch; the placeholder's own text never reaches the output because every text node in the row is a
/// slot.
const placeholderSetting: SettingEntry = {
  key: "setting.key",
  kind: "text",
  tier: "secret",
  default: "",
  description_en: "",
  description_th: "",
};

function Field({ mode, setting }: { mode: Mode; setting: SettingEntry }) {
  const id = setting.key.replace(/\./g, "-");

  if (mode === "template") {
    return <div className="form-control" dangerouslySetInnerHTML={{ __html: templateRow() }} />;
  }
  return (
    <div className="form-control">
      <Label id={id} entry={setting} />
      {setting.tier === "secret" ? <SecretInput id={id} setting={setting} /> : <PlainInput id={id} setting={setting} />}
    </div>
  );
}

function Label({ id, entry }: { id: string; entry: SettingEntry }) {
  return (
    <label className="label" htmlFor={id}>
      <span className="label-text font-mono text-sm">{entry.key}</span>
      <span className="label-text-alt opacity-60">{entry.tier}</span>
    </label>
  );
}

function SecretInput({ id, setting }: { id: string; setting: SettingEntry }) {
  return (
    <>
      <input
        id={id}
        name={setting.key}
        type="password"
        className="input input-bordered w-full"
        autoComplete="new-password"
      />
      <label className="label">
        <span className="label-text-alt">{SECRET_HINT}</span>
      </label>
    </>
  );
}

function PlainInput({ id, setting }: { id: string; setting: SettingEntry }) {
  if (setting.kind === "bool") {
    return <input id={id} name={setting.key} type="checkbox" className="toggle" defaultChecked={setting.default === "true"} />;
  }
  return (
    <input id={id} name={setting.key} type="text" className="input input-bordered w-full" defaultValue={setting.default} />
  );
}

/// The template-mode row: the same markup, with the tier branch emitted around the input. The password input
/// carries no `value` attribute at all — that is the read-back rule, expressed once.
function templateRow(): string {
  const mode: Mode = "template";
  const label = renderToStaticMarkup(
    <label className="label" htmlFor={slot(mode, "id", "x")}>
      <span className="label-text">{slot(mode, "setting.description", "")}</span>
      <span className="label-text-alt opacity-60">{slot(mode, "setting.tier", "")}</span>
    </label>,
  );
  const secret = renderToStaticMarkup(
    <>
      <input
        id={slot(mode, "id", "x")}
        name={slot(mode, "setting.key", "")}
        type="password"
        className="input input-bordered w-full"
        autoComplete="new-password"
      />
      <label className="label">
        <span className="label-text-alt">{slot(mode, "secret_hint", SECRET_HINT)}</span>
      </label>
    </>,
  );
  const plain = renderToStaticMarkup(
    <input
      id={slot(mode, "id", "x")}
      name={slot(mode, "setting.key", "")}
      type="text"
      className="input input-bordered w-full"
      defaultValue={slot(mode, "setting.value", "")}
    />,
  );
  return (
    label +
    when(mode, 'setting.tier == "secret"') +
    secret +
    otherwise(mode) +
    plain +
    endWhen(mode)
  );
}
