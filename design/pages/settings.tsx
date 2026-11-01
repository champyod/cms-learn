import { renderToStaticMarkup } from "react-dom/server";
import { endWhen, loopEnd, loopStart, otherwise, slot, when, type Mode } from "../slots.ts";
import { groupOf, type SettingsFixture, type SettingEntry } from "../catalogue.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

// The settings form, generated from the catalogue rather than written per key: adding a setting is a change
// to deploy/catalogue/settings.json and nothing else.
//
// A key's first segment is its group, and one request renders one group: the group travels in the query
// string, the controller passes that group's entries as `settings`, and the form posts only what it showed.
// The nav is the other half of that contract — `groups` and `active_group` — so a reader can reach a group
// the current request did not render.
//
// A secret is the one field with a rule attached. Its value is never rendered — the field arrives empty, a
// change replaces it, and the page says so. The branch that enforces this is emitted into the template, so
// the rule lives in one place and applies to every secret the catalogue declares.

interface SettingsGroup {
  key: string;
  settings: SettingEntry[];
}

/// Splits the catalogue by first key segment, keeping the order the catalogue declares.
function groupSettings(settings: SettingEntry[]): SettingsGroup[] {
  const groups: SettingsGroup[] = [];
  for (const setting of settings) {
    const key = groupOf(setting.key);
    const existing = groups.find((group) => group.key === key);
    if (existing === undefined) {
      groups.push({ key, settings: [setting] });
      continue;
    }
    existing.settings.push(setting);
  }
  return groups;
}

export function Settings({ mode, data }: { mode: Mode; data: SettingsFixture }) {
  const groups = groupSettings(data.settings);
  const active = groups.find((group) => group.key === data.group) ?? groups[0];

  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.title}
      body={
        <>
          <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "title", data.title)}</h1>
          <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>

          <GroupNav mode={mode} groups={groups} active={active?.key ?? ""} />

          <form className="mt-8 grid gap-4" method="post" action={slot(mode, "action", "/settings")}>
            <RepeatList
              mode={mode}
              tag="ul"
              className="contents"
              alias="setting"
              collection="settings"
              items={active === undefined ? [] : active.settings}
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

/// One link per group. The selected group is a query parameter rather than a path segment, so the form's
/// action and the nav share the same address and no second route has to exist.
function GroupNav({ mode, groups, active }: { mode: Mode; groups: SettingsGroup[]; active: string }) {
  if (mode === "template") {
    return <nav className="tabs tabs-box mt-6" dangerouslySetInnerHTML={{ __html: templateNav() }} />;
  }
  return (
    <nav className="tabs tabs-box mt-6">
      {groups.map((group) => (
        <a
          key={group.key}
          className={group.key === active ? "tab tab-active" : "tab"}
          href={`?group=${group.key}`}
        >
          {group.key}
        </a>
      ))}
    </nav>
  );
}

/// The nav as Inja: `groups` is the list of first segments, `active_group` the one this request renders.
function templateNav(): string {
  const mode: Mode = "template";
  const link = (isActive: boolean): string =>
    renderToStaticMarkup(
      <a className={isActive ? "tab tab-active" : "tab"} href="?group={{ group.key }}">
        {slot(mode, "group.key", "")}
      </a>,
    );
  const branch =
    when(mode, "group.key == active_group") + link(true) + otherwise(mode) + link(false) + endWhen(mode);
  return loopStart(mode, "group", "groups") + branch + loopEnd(mode);
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
  // The field's id is its key in both renderings: the template can only echo `setting.key`, so deriving a
  // different shape here would give the design page and the shipped page different ids for the same field.
  const id = setting.key;

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
    <label className="label" htmlFor={slot(mode, "setting.key", "x")}>
      <span className="label-text">{slot(mode, "setting.description", "")}</span>
      <span className="label-text-alt opacity-60">{slot(mode, "setting.tier", "")}</span>
    </label>,
  );
  const secret = renderToStaticMarkup(
    <>
      <input
        id={slot(mode, "setting.key", "x")}
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
      id={slot(mode, "setting.key", "x")}
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
