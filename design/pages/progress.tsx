import { endWhen, otherwise, slot, when, type Mode } from "../slots.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

// The learner's own record: what they touched, what passed, and nothing that the schema cannot support.
//
// A topic with no rows is "untried", never "weak": no column, no threshold and no ranking exists for the
// latter, so the page says that in words rather than inventing a judgement it would have to defend.

export interface TopicProgressRow {
  id: string;
  label: string;
  attempted: number;
  solved: number;
  is_untried: boolean;
}

export interface ProgressFixture {
  site_name: string;
  heading: string;
  lede: string;
  attempted_label: string;
  solved_label: string;
  untried_label: string;
  topic_heading: string;
  no_threshold: string;
  empty_label: string;
  latest_label: string;
  attempted: number;
  solved: number;
  last_attempted_on: string;
  last_solved_on: string;
  has_activity: boolean;
  has_topics: boolean;
  topics: TopicProgressRow[];
}

export const progressFixture: ProgressFixture = {
  site_name: "CMS Learn",
  heading: "ความคืบหน้า",
  lede: "ทุกตัวเลขในหน้านี้มาจากแถวที่บันทึกไว้จริง",
  attempted_label: "กำลังทำ",
  solved_label: "ผ่านแล้ว",
  untried_label: "ยังไม่เคยลอง",
  topic_heading: "หัวข้อ",
  no_threshold:
    "หัวข้อที่ยังไม่เคยลองไม่ใช่หัวข้อที่อ่อนแอ เพราะยังไม่มีเกณฑ์ใดในระบบที่ตัดสินว่าอ่อนหรือแข็ง",
  empty_label: "ยังไม่มีการส่งใด ๆ",
  latest_label: "ล่าสุด",
  attempted: 2,
  solved: 1,
  last_attempted_on: "2026-11-10",
  last_solved_on: "2026-11-09",
  has_activity: true,
  has_topics: true,
  topics: [
    { id: "dp", label: "กำหนดการพลวัต", attempted: 1, solved: 1, is_untried: false },
    { id: "sorting", label: "การเรียงลำดับ", attempted: 1, solved: 0, is_untried: false },
    { id: "io", label: "การรับเข้าและส่งออก", attempted: 0, solved: 0, is_untried: true },
  ],
};

const placeholderTopic: TopicProgressRow = {
  id: "t",
  label: "",
  attempted: 0,
  solved: 0,
  is_untried: false,
};

export function Progress({ mode, data }: { mode: Mode; data: ProgressFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.heading}
      body={<Body mode={mode} data={data} />}
    />
  );
}

function Body({ mode, data }: { mode: Mode; data: ProgressFixture }) {
  return (
    <>
      <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "heading", data.heading)}</h1>
      <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>

      {when(mode, "has_activity")}
      <dl className="mt-6 grid grid-cols-2 gap-4 text-sm sm:grid-cols-4">
        <div className="card border border-base-300 bg-base-200 p-4">
          <dt className="opacity-70">{slot(mode, "attempted_label", data.attempted_label)}</dt>
          <dd className="text-xl font-semibold">{slot(mode, "attempted", String(data.attempted))}</dd>
        </div>
        <div className="card border border-base-300 bg-base-200 p-4">
          <dt className="opacity-70">{slot(mode, "solved_label", data.solved_label)}</dt>
          <dd className="text-xl font-semibold">{slot(mode, "solved", String(data.solved))}</dd>
        </div>
        <div className="card border border-base-300 bg-base-200 p-4">
          <dt className="opacity-70">{slot(mode, "latest_label", data.latest_label)}</dt>
          <dd className="text-sm">{slot(mode, "last_attempted_on", data.last_attempted_on)}</dd>
        </div>
        <div className="card border border-base-300 bg-base-200 p-4">
          <dt className="opacity-70">
            {slot(mode, "solved_label", data.solved_label)}{" "}
            {slot(mode, "latest_label", data.latest_label)}
          </dt>
          <dd className="text-sm">{slot(mode, "last_solved_on", data.last_solved_on)}</dd>
        </div>
      </dl>
      {otherwise(mode)}
      <p className="mt-6 opacity-70">{slot(mode, "empty_label", data.empty_label)}</p>
      {endWhen(mode)}

      {when(mode, "has_topics")}
      <section className="mt-10">
        <h2 className="text-xl font-semibold">{slot(mode, "topic_heading", data.topic_heading)}</h2>
        <table className="table mt-4">
          <thead>
            <tr>
              <th>{slot(mode, "topic_heading", data.topic_heading)}</th>
              <th>{slot(mode, "attempted_label", data.attempted_label)}</th>
              <th>{slot(mode, "solved_label", data.solved_label)}</th>
            </tr>
          </thead>
          <RepeatList
            mode={mode}
            tag="tbody"
            alias="topic"
            collection="topics"
            items={data.topics}
            placeholder={placeholderTopic}
          >
            {(topic) => (
              <tr>
                <td>{slot(mode, "topic.label", topic.label)}</td>
                <td>{slot(mode, "topic.attempted", String(topic.attempted))}</td>
                <td>
                  {when(mode, "topic.is_untried")}
                  <span className="badge badge-sm badge-ghost">
                    {slot(mode, "untried_label", data.untried_label)}
                  </span>
                  {otherwise(mode)}
                  {slot(mode, "topic.solved", String(topic.solved))}
                  {endWhen(mode)}
                </td>
              </tr>
            )}
          </RepeatList>
        </table>
      </section>
      {endWhen(mode)}

      <p className="mt-8 text-sm opacity-70">{slot(mode, "no_threshold", data.no_threshold)}</p>
    </>
  );
}
