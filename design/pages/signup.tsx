import { slot, type Mode } from "../slots.ts";
import type { SignupFixture } from "../fixtures.ts";
import { Layout } from "./layout.tsx";

// The sign-up page. Like the sign-in form it writes no value attribute, so a refused submission is shown a
// message and never the password that was typed.
export function Signup({ mode, data }: { mode: Mode; data: SignupFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.title}
      body={
        <>
          <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "title", data.title)}</h1>
          <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>
          <p className="mt-4 text-sm text-error">{slot(mode, "message", data.message)}</p>

          <form
            className="mt-6 grid max-w-sm gap-4"
            method="post"
            action={slot(mode, "action", "/signup")}
          >
            <label className="form-control">
              <span className="label-text">{slot(mode, "email_label", data.email_label)}</span>
              <input
                className="input input-bordered w-full"
                type="email"
                name="email"
                autoComplete="email"
                required
              />
            </label>
            <label className="form-control">
              <span className="label-text">{slot(mode, "handle_label", data.handle_label)}</span>
              <input
                className="input input-bordered w-full"
                type="text"
                name="handle"
                autoComplete="username"
                required
              />
            </label>
            <label className="form-control">
              <span className="label-text">{slot(mode, "password_label", data.password_label)}</span>
              <input
                className="input input-bordered w-full"
                type="password"
                name="password"
                autoComplete="new-password"
                required
              />
            </label>
            <button className="btn btn-primary mt-2" type="submit">
              {slot(mode, "submit_label", data.submit_label)}
            </button>
          </form>

          <p className="mt-6 text-sm">
            <a className="link link-hover" href={slot(mode, "signin_href", "/signin")}>
              {slot(mode, "signin_label", data.signin_label)}
            </a>
          </p>
        </>
      }
    />
  );
}
