import { endWhen, loopEnd, loopStart, otherwise, slot, when, type Mode } from "../slots.ts";
import type { AdminFixture, AdminFixtureRow, LearnerRow, RoleFixture } from "../fixtures.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

// The administrator panel. One page, a roster and five forms, each worth one sentence: the server owns the
// authority, the token comparison and the audit row, so the page only carries the fields those actions read.
//
// Every form carries the session's token, because a missing token must be a refusal rather than a sign that
// nobody is signed in.

const placeholderRole: RoleFixture = { id: "", name: "" };

const placeholderAdministrator: AdminFixtureRow = { id: "", handle: "" };

const placeholderLearner: LearnerRow = {
  id: "",
  handle: "",
  status: "",
  level: "",
};

export function AdminHome({ mode, data }: { mode: Mode; data: AdminFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.title}
      body={
        <>
          <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "title", data.title)}</h1>
          <p className="mt-1 opacity-70">
            {slot(mode, "lede", data.lede)} <strong>{slot(mode, "admin_handle", data.admin_handle)}</strong>
          </p>
          {when(mode, "has_notice")}
          <p className="mt-4 text-sm text-success">{slot(mode, "notice", data.notice)}</p>
          {endWhen(mode)}
          {when(mode, "has_message")}
          <p className="mt-4 text-sm text-error">{slot(mode, "message", data.message)}</p>
          {endWhen(mode)}

          <section className="mt-10">
            <h2 className="text-xl font-semibold">{slot(mode, "roster_label", data.roster_label)}</h2>
            {when(mode, "has_learners")}
            <table className="table mt-4">
              <thead>
                <tr>
                  <th>{slot(mode, "id_label", data.id_label)}</th>
                  <th>{slot(mode, "handle_label", data.handle_label)}</th>
                  <th>{slot(mode, "status_label", data.status_label)}</th>
                  <th>{slot(mode, "level_label", data.level_label)}</th>
                </tr>
              </thead>
              <RepeatList
                mode={mode}
                tag="tbody"
                alias="learner"
                collection="learners"
                items={data.learners}
                placeholder={placeholderLearner}
              >
                {(learner) => (
                  <tr>
                    <td className="font-mono text-xs">{slot(mode, "learner.id", learner.id)}</td>
                    <td>{slot(mode, "learner.handle", learner.handle)}</td>
                    <td>{slot(mode, "learner.status", learner.status)}</td>
                    <td>{slot(mode, "learner.level", learner.level)}</td>
                  </tr>
                )}
              </RepeatList>
            </table>
            {otherwise(mode)}
            <p className="mt-4 opacity-70">{slot(mode, "empty_label", data.empty_label)}</p>
            {endWhen(mode)}
          </section>

          <section className="mt-10 grid gap-8 sm:grid-cols-2">
            <form className="grid gap-4" action="/admin/create-learner" method="post">
              <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
              <h2 className="text-lg font-semibold">{slot(mode, "create_learner_label", data.create_learner_label)}</h2>
              <Field mode={mode} label={slot(mode, "handle_label", data.handle_label)} name="handle" kind="text" required />
              <Field mode={mode} label={slot(mode, "email_label", data.email_label)} name="email" kind="email" />
              <Field mode={mode} label={slot(mode, "level_label", data.level_label)} name="level_id" kind="text" />
              <Field mode={mode} label={slot(mode, "password_label", data.password_label)} name="password" kind="password" />
              <button className="btn btn-primary mt-2" type="submit">
                {slot(mode, "submit_label", data.submit_label)}
              </button>
            </form>

            <form className="grid gap-4" action="/admin/create-admin" method="post">
              <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
              <h2 className="text-lg font-semibold">{slot(mode, "create_admin_label", data.create_admin_label)}</h2>
              <Field mode={mode} label={slot(mode, "handle_label", data.handle_label)} name="handle" kind="text" required />
              <Field mode={mode} label={slot(mode, "role_label", data.role_label)} name="roles" kind="text" />
              <Field mode={mode} label={slot(mode, "password_label", data.password_label)} name="password" kind="password" />
              <button className="btn btn-primary mt-2" type="submit">
                {slot(mode, "submit_label", data.submit_label)}
              </button>
            </form>

            <form className="grid gap-4" action="/admin/approve" method="post">
              <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
              <h2 className="text-lg font-semibold">{slot(mode, "approve_label", data.approve_label)}</h2>
              <Field mode={mode} label={slot(mode, "learner_id_label", data.learner_id_label)} name="learner_id" kind="text" required />
              <button className="btn btn-primary mt-2" type="submit">
                {slot(mode, "submit_label", data.submit_label)}
              </button>
            </form>

            <form className="grid gap-4" action="/admin/revoke-admin" method="post">
              <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
              <h2 className="text-lg font-semibold">{slot(mode, "revoke_admin_label", data.revoke_admin_label)}</h2>
              <Field mode={mode} label={slot(mode, "admin_id_label", data.admin_id_label)} name="admin_id" kind="text" required />
              <button className="btn btn-primary mt-2" type="submit">
                {slot(mode, "submit_label", data.submit_label)}
              </button>
            </form>

            <form className="grid gap-4" action="/admin/issue-verification" method="post">
              <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
              <h2 className="text-lg font-semibold">
                {slot(mode, "issue_verification_label", data.issue_verification_label)}
              </h2>
              <Field mode={mode} label={slot(mode, "learner_id_label", data.learner_id_label)} name="learner_id" kind="text" required />
              <button className="btn btn-primary mt-2" type="submit">
                {slot(mode, "submit_label", data.submit_label)}
              </button>
            </form>

        <section className="mt-10">
          <h2 className="text-xl font-semibold">
            {slot(mode, "grant_role_label", data.grant_role_label)}
          </h2>
          <p className="mt-1 opacity-70">{slot(mode, "role_name_label", data.role_name_label)}</p>
          <RepeatList
            mode={mode}
            tag="ul"
            className="mt-2 flex flex-wrap gap-2 text-sm"
            alias="role"
            collection="roles"
            items={data.roles}
            placeholder={placeholderRole}
          >
            {(role) => (
              <li className="badge badge-sm badge-outline">
                {slot(mode, "role.name", role.name)} · {slot(mode, "role.id", role.id)}
              </li>
            )}
          </RepeatList>
          <RepeatList
            mode={mode}
            tag="ul"
            className="mt-2 flex flex-wrap gap-2 text-sm"
            alias="administrator"
            collection="administrators"
            items={data.administrators}
            placeholder={placeholderAdministrator}
          >
            {(administrator) => (
              <li className="badge badge-sm badge-outline">
                {slot(mode, "administrator.handle", administrator.handle)} ·{" "}
                {slot(mode, "administrator.id", administrator.id)}
              </li>
            )}
          </RepeatList>
          <form className="mt-4 grid gap-4" action="/admin/grant-role" method="post">
            <input type="hidden" name="csrf_token" value={slot(mode, "csrf_token", data.csrf_token)} />
            <Field
              mode={mode}
              label={slot(mode, "admin_id_label", data.admin_id_label)}
              name="admin_id"
              kind="text"
              required
            />
            <Field
              mode={mode}
              label={slot(mode, "role_name_label", data.role_name_label)}
              name="role_id"
              kind="text"
              required
            />
            <button className="btn btn-primary w-fit" type="submit">
              {slot(mode, "submit_label", data.submit_label)}
            </button>
          </form>
        </section>
          </section>
        </>
      }
    />
  );
}

interface FieldProps {
  mode: Mode;
  label: string;
  name: string;
  kind: "text" | "email" | "password";
  required?: boolean;
}

/// One labelled input. A password input never carries a value, so no rendered page can echo one back.
function Field({ mode, label, name, kind, required = false }: FieldProps) {
  return (
    <label className="form-control">
      <span className="label-text">{label}</span>
      <input
        className="input input-bordered w-full"
        type={kind}
        autoComplete={kind === "password" ? "new-password" : "off"}
        required={required}
        name={name}
      />
    </label>
  );
}
