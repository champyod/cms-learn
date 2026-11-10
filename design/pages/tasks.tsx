// The all-tasks page: one list, two filters, and the sentence a learner reads when the two empty it.
//
// The rungs the filter offers are the rows the shell's meter draws (the server reads both from the same
// level table), so this page holds no copy of a band: the words and the numbers arrive in the model.
import { endWhen, otherwise, slot, when, type Mode } from "../slots.ts";
import type { LevelChoice, TaskListFixture, TaskListRow, TopicChoice } from "../fixtures.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

const placeholderTask: TaskListRow = { id: "t", title: "", difficulty: 0 };
const placeholderRung: LevelChoice = { id: "r", title: "", band: "", value: "", is_chosen: false };
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
          <RepeatList
            mode={mode}
            tag="ul"
            className="flex flex-wrap gap-3 text-sm"
            alias="rung"
            collection="levels"
            items={data.levels}
            placeholder={placeholderRung}
          >
            {(rung) => (
              <li>
                <label className="flex items-center gap-1">
                  <input type="radio" name="level" value={slot(mode, "rung.value", rung.value)} />
                  {slot(mode, "rung.title", rung.title)} ({slot(mode, "rung.band", rung.band)})
                </label>
              </li>
            )}
          </RepeatList>
          <label className="flex items-center gap-1">
            <input type="radio" name="level" value="" />
            {slot(mode, "any_level_label", data.any_level_label)}
          </label>
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
        {slot(mode, "filter_level_name", data.filter_level_name)} &middot;{" "}
        {slot(mode, "topics_line", data.topics_line)}
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
