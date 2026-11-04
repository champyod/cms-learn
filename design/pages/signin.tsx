import { slot, type Mode } from "../slots.ts";
import type { SigninFixture } from "../fixtures.ts";
import { Layout } from "./layout.tsx";

// The sign-in page. Neither input carries a default: the form is written again empty on every visit, so a
// password can never be echoed back into the field it came from.
export function Signin({ mode, data }: { mode: Mode; data: SigninFixture }) {
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
            action={slot(mode, "action", "/signin")}
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
              <span className="label-text">{slot(mode, "password_label", data.password_label)}</span>
              <input
                className="input input-bordered w-full"
                type="password"
                name="password"
                autoComplete="current-password"
                required
              />
            </label>
            <button className="btn btn-primary mt-2" type="submit">
              {slot(mode, "submit_label", data.submit_label)}
            </button>
          </form>

          <p className="mt-6 text-sm">
            <a className="link link-hover" href={slot(mode, "signup_href", "/signup")}>
              {slot(mode, "signup_label", data.signup_label)}
            </a>
          </p>
        </>
      }
    />
  );
}
