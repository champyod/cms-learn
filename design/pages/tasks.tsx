// The all-tasks page: one list, two filters, and the sentence a learner reads when the two empty it.
//
// The level control is generated from content/levels.json at port time — the file the content gate validates
// and the control the learner clicks cannot disagree, and no band is repeated as a literal here.
import { Fragment } from "react";
import levels from "../../content/levels.json";
import { endWhen, otherwise, slot, when, type Mode } from "../slots.ts";
import type { TaskListFixture, TaskListRow, TopicChoice } from "../fixtures.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

interface Rung {
  id: string;
  title: string;
  min: number;
  max: number;
  value: string;
  band: string;
}

// A rung is a band over the one difficulty scale (ADR 0013 §3): the value a request carries is that band,
// and the filter is the number, never a stored level.
const rungs: Rung[] = levels.levels.map((rung) => ({
  id: rung.id,
  title: rung.title.en,
  min: rung.min,
  max: rung.max,
  value: `${rung.min}-${rung.max}`,
  band: `${rung.min}\u2013${rung.max}`,
}));

const placeholderTask: TaskListRow = { id: "t", title: "", difficulty: 0 };
const placeholderTopic: TopicChoice = { id: "t", label: "" };

export function Tasks({ mode, data }: { mode: Mode; data: TaskListFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.heading}
      body={<Body mode={mode} data={data} />}
    />
  );
}

function Body({ mode, data }: { mode: Mode; data: TaskListFixture }) {
  return (
    <>
      <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "heading", data.heading)}</h1>
      <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>

      {when(mode, "has_stage")}
      <p className="mt-4 text-sm opacity-70">{slot(mode, "stage_note", data.stage_note)}</p>
      {endWhen(mode)}

      <form className="mt-6 grid gap-6" action="/tasks" method="get">
        {when(mode, "has_stage")}
        <input type="hidden" name="track" value={slot(mode, "stage_track", "t")} />
        <input type="hidden" name="stage" value={slot(mode, "stage_id", "s")} />
        {endWhen(mode)}

        <fieldset className="grid gap-2">
          <legend className="text-sm font-semibold">{slot(mode, "level_label", data.level_label)}</legend>
          <div className="flex flex-wrap gap-3 text-sm">
            <label className="flex items-center gap-1">
              <input type="radio" name="level" value="" />
              {slot(mode, "any_level_label", data.any_level_label)}
            </label>
            {rungs.map((rung) => (
              <label className="flex items-center gap-1" key={rung.id}>
                <input type="radio" name="level" value={rung.value} />
                {rung.title} ({rung.band})
              </label>
            ))}
          </div>
        </fieldset>

        <fieldset className="grid gap-2">
          <legend className="text-sm font-semibold">{slot(mode, "topics_label", data.topics_label)}</legend>
          <RepeatList
            mode={mode}
            tag="ul"
            className="flex flex-wrap gap-3 text-sm"
            alias="topic"
            collection="topics"
            items={data.topics}
            placeholder={placeholderTopic}
          >
            {(topic) => (
              <li>
                <label className="flex items-center gap-1">
                  <input type="checkbox" name={"topic:" + slot(mode, "topic.id", topic.id)} value="on" />
                  {slot(mode, "topic.label", topic.label)}
                </label>
              </li>
            )}
          </RepeatList>
        </fieldset>

        <button className="btn btn-sm btn-primary justify-self-start" type="submit">
          {slot(mode, "apply_label", data.apply_label)}
        </button>
      </form>

      <p className="mt-6 text-sm opacity-70">
        {slot(mode, "current_label", data.current_label)}:{" "}
        <LevelName mode={mode} anyLabel={data.any_level_label} sample={rungs[0].title} />{" "}
        &middot; {slot(mode, "topics_line", data.topics_line)}
      </p>

      {when(mode, "has_tasks")}
      <RepeatList
        mode={mode}
        tag="ul"
        className="mt-6 grid gap-3"
        alias="task"
        collection="tasks"
        items={data.tasks}
        placeholder={placeholderTask}
      >
        {(task) => (
          <li className="card border border-base-300 bg-base-200">
            <div className="card-body flex-row items-center justify-between gap-3 p-4">
              <a className="link link-hover font-medium" href={"/tasks/" + slot(mode, "task.id", task.id)}>
                {slot(mode, "task.title", task.title)}
              </a>
              <span className="text-sm opacity-70">
                {slot(mode, "difficulty_label", data.difficulty_label)}{" "}
                {slot(mode, "task.difficulty", String(task.difficulty))}
              </span>
            </div>
          </li>
        )}
      </RepeatList>
      {otherwise(mode)}
      <p className="mt-6 opacity-70">{slot(mode, "empty_label", data.empty_label)}</p>
      {endWhen(mode)}
    </>
  );
}

// Names the rung the request is filtered by. The chain is emitted from the rung list, so the page cannot
// name a rung the console does not define; both bounds are zero on an unfiltered page and the innermost
// branch is the "any level" one. Inja has no `elif`, so each later rung is a nested conditional, and the
// closers are counted from the same list.
function LevelName({ mode, anyLabel, sample }: { mode: Mode; anyLabel: string; sample: string }) {
  if (mode === "design") {
    return <>{sample}</>;
  }
  return (
    <>
      {rungs.map((rung, index) => (
        <Fragment key={rung.id}>
          {index === 0 ? null : otherwise(mode)}
          {when(mode, `level_min == ${rung.min} and level_max == ${rung.max}`)}
          {rung.title}
        </Fragment>
      ))}
      {otherwise(mode)}
      {slot(mode, "any_level_label", anyLabel)}
      {rungs.map((rung) => (
        <Fragment key={rung.id}>{endWhen(mode)}</Fragment>
      ))}
    </>
  );
}
