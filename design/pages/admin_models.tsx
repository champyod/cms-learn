import { endWhen, loopEnd, loopStart, otherwise, slot, when, type Mode } from "../slots.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";
import type { ModelRow, ModelsFixture } from "../fixtures.ts";

// The model records the worker calls. A model is data, not a protocol frozen in code (ADR 0055): the page
// lists what the table holds, offers enable, disable and make-default per row, and adds a record with its
// key entered once. The key is written sealed and never rendered, so the form has no value to echo.
//
// The state cell is three independent branches and the action cell is two forms, because Inja compares one
// model at a time: the markers sit inside the cells, never around a wrapper that would change the table.

const placeholderModel: ModelRow = {
  name: "",
  base_url: "",
  protocol: "",
  model_id: "",
  is_default: false,
  enabled: false,
};

export function AdminModels({ mode, data }: { mode: Mode; data: ModelsFixture }) {
  const verb = (enabled: boolean): string =>
    when(mode, enabled ? "model.enabled" : "not model.enabled") +
    (enabled ? "disable" : "enable") +
    otherwise(mode) +
    (enabled ? "enable" : "disable") +
    endWhen(mode);
  const actionLabel = (enabled: boolean): string =>
    when(mode, enabled ? "model.enabled" : "not model.enabled") +
    slot(mode, enabled ? "disable_label" : "enable_label", enabled ? data.disable_label : data.enable_label) +
    otherwise(mode) +
    slot(mode, enabled ? "enable_label" : "disable_label", enabled ? data.enable_label : data.disable_label) +
    endWhen(mode);

  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.title}
      body={
        <>
          <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "title", data.title)}</h1>
          <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>
          {when(mode, "has_notice")}
          <p className="mt-4 rounded-box bg-base-200 p-3 text-sm">{slot(mode, "notice", data.notice)}</p>
          {endWhen(mode)}

          <section className="mt-10">
            <h2 className="text-xl font-semibold">{slot(mode, "list_title", data.list_title)}</h2>
            <table className="table mt-4">
              <thead>
                <tr>
                  <th>{slot(mode, "name_label", data.name_label)}</th>
                  <th>{slot(mode, "address_label", data.address_label)}</th>
                  <th>{slot(mode, "protocol_label", data.protocol_label)}</th>
                  <th>{slot(mode, "model_label", data.model_label)}</th>
                  <th>{slot(mode, "state_label", data.state_label)}</th>
                  <th />
                </tr>
              </thead>
              <RepeatList
                mode={mode}
                tag="tbody"
                alias="model"
                collection="models"
                items={data.models}
                placeholder={placeholderModel}
              >
                {(model) => (
                  <tr>
                    <td className="font-mono text-xs">{slot(mode, "model.name", model.name)}</td>
                    <td className="font-mono text-xs">{slot(mode, "model.base_url", model.base_url)}</td>
                    <td>{slot(mode, "model.protocol", model.protocol)}</td>
                    <td className="font-mono text-xs">{slot(mode, "model.model_id", model.model_id)}</td>
                    <td>
                      {when(mode, "model.is_default")}
                      {slot(mode, "default_label", data.default_label)}
                      {endWhen(mode)}
                      {when(mode, "model.enabled")}
                      {slot(mode, "enabled_label", data.enabled_label)}
                      {endWhen(mode)}
                      {when(mode, "not model.enabled")}
                      {slot(mode, "disabled_label", data.disabled_label)}
                      {endWhen(mode)}
                    </td>
                    <td className="text-right">
                      <form className="inline" action={slot(mode, "action", data.action)} method="post">
                        <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
                        <input type="hidden" name="model" value={slot(mode, "model.name", model.name)} />
                        <input type="hidden" name="verb" value={verb(true)} />
                        <button className="btn btn-xs" type="submit">
                          {actionLabel(true)}
                        </button>
                      </form>
                      {when(mode, "not model.is_default")}
                      <form className="inline ms-1" action={slot(mode, "action", data.action)} method="post">
                        <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
                        <input type="hidden" name="model" value={slot(mode, "model.name", model.name)} />
                        <input type="hidden" name="verb" value="default" />
                        <button className="btn btn-xs btn-primary" type="submit">
                          {slot(mode, "pick_label", data.pick_label)}
                        </button>
                      </form>
                      {endWhen(mode)}
                    </td>
                  </tr>
                )}
              </RepeatList>
            </table>
            {when(mode, "has_no_models")}
            <p className="mt-4 opacity-70">{slot(mode, "empty_label", data.empty_label)}</p>
            {endWhen(mode)}
          </section>

          <section className="mt-10">
            <h2 className="text-xl font-semibold">{slot(mode, "add_title", data.add_title)}</h2>
            <form className="mt-4 grid gap-4" action={slot(mode, "action", data.action)} method="post">
              <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
              <input type="hidden" name="verb" value="add" />
              <Field mode={mode} label={slot(mode, "name_label", data.name_label)} name="name" required />
              <Field
                mode={mode}
                label={slot(mode, "address_label", data.address_label)}
                name="base_url"
                kind="url"
                required
              />
              <label className="form-control">
                <span className="label-text">{slot(mode, "protocol_label", data.protocol_label)}</span>
                <select
                  className="select select-bordered"
                  name="protocol"
                  dangerouslySetInnerHTML={{ __html: protocolOptions(mode, data.protocols) }}
                />
              </label>
              <Field mode={mode} label={slot(mode, "model_label", data.model_label)} name="model_id" required />
              <label className="form-control">
                <span className="label-text">{slot(mode, "key_label", data.key_label)}</span>
                <input
                  className="input input-bordered"
                  type="password"
                  autoComplete="new-password"
                  name="key"
                  required
                />
                <span className="label-text-alt opacity-60">{slot(mode, "key_hint", data.key_hint)}</span>
              </label>
              <label className="label cursor-pointer justify-start gap-3">
                <input className="checkbox" type="checkbox" name="is_default" value="true" />
                <span className="label-text">{slot(mode, "make_default_label", data.make_default_label)}</span>
              </label>
              <button className="btn btn-primary mt-2" type="submit">
                {slot(mode, "add_label", data.add_label)}
              </button>
            </form>
          </section>
        </>
      }
    />
  );
}

/// The protocol choices as Inja: a loop over `protocols` in template mode, the fixture's options in design mode.
/// A map over the fixture cannot emit the loop, which left the template with one option and an unbound variable.
function protocolOptions(mode: Mode, protocols: string[]): string {
  if (mode === "template") {
    const template: Mode = "template";
    const option = `<option value="${slot(template, "protocol", "")}">${slot(template, "protocol", "")}</option>`;
    return loopStart(template, "protocol", "protocols") + option + loopEnd(template);
  }
  return protocols.map((protocol) => `<option value="${protocol}">${protocol}</option>`).join("");
}

interface FieldProps {
  mode: Mode;
  label: string;
  name: string;
  kind?: "text" | "url";
  required?: boolean;
}

function Field({ mode, label, name, kind = "text", required = false }: FieldProps) {
  return (
    <label className="form-control">
      <span className="label-text">{label}</span>
      <input
        className="input input-bordered"
        type={kind}
        name={name}
        required={required}
        pattern={name === "name" ? "[a-z][a-z0-9_-]{0,63}" : undefined}
      />
    </label>
  );
}
