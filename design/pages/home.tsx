import { slot, type Mode } from "../slots.ts";
import type { PageFixture, StageFixture, TrackFixture } from "../fixtures.ts";
import { RepeatList } from "../repeat.tsx";
import { Layout } from "./layout.tsx";

// The curriculum page. Written once: every value goes through a slot, so the design gallery shows the
// fixture's words and the port emits the Inja expression for the same place. There is no second copy of
// this markup to keep in step.

const placeholderStage: StageFixture = {
  id: "s",
  title_en: "",
  title_th: "",
  difficulty_min: 0,
  difficulty_max: 0,
  tags: ["tag"],
};

const placeholderTrack: TrackFixture = {
  id: "t",
  title_en: "",
  title_th: "",
  description_en: "",
  description_th: "",
  stages: [],
};

export function Home({ mode, data }: { mode: Mode; data: PageFixture }) {
  return (
    <Layout
      mode={mode}
      site_name={data.site_name}
      heading={data.heading}
      body={<Body mode={mode} data={data} />}
    />
  );
}

function Body({ mode, data }: { mode: Mode; data: PageFixture }) {
  return (
    <>
      <h1 className="text-2xl font-semibold tracking-tight">{slot(mode, "heading", data.heading)}</h1>
      <p className="mt-1 opacity-70">{slot(mode, "lede", data.lede)}</p>

      <RepeatList
        mode={mode}
        tag="ul"
        className="contents"
        alias="track"
        collection="tracks"
        items={data.tracks}
        placeholder={placeholderTrack}
      >
        {(track) => (
          <section className="mt-10" id={slot(mode, "track.id", track.id)}>
            <h2 className="text-xl font-semibold">{slot(mode, "track.title", track.title_th)}</h2>
            <p className="mt-1 opacity-70">
              {slot(mode, "track.description", track.description_th)}
            </p>
            <StageList mode={mode} data={data} stages={track.stages} />
          </section>
        )}
      </RepeatList>
    </>
  );
}

function StageList({
  mode,
  data,
  stages,
}: {
  mode: Mode;
  data: PageFixture;
  stages: StageFixture[];
}) {
  return (
    <RepeatList
      mode={mode}
      tag="ol"
      className="mt-4 grid gap-4 sm:grid-cols-2 lg:grid-cols-3"
      alias="stage"
      collection="track.stages"
      items={stages}
      placeholder={placeholderStage}
    >
      {(stage) => (
        <li className="card border border-base-300 bg-base-200">
          <div className="card-body gap-2 p-4">
            <h3 className="card-title text-base">{slot(mode, "stage.title", stage.title_th)}</h3>
            <p className="text-sm opacity-70">
              {slot(mode, "band_label", data.band_label)}{" "}
              {slot(mode, "stage.difficulty_min", String(stage.difficulty_min))}&ndash;
              {slot(mode, "stage.difficulty_max", String(stage.difficulty_max))}
            </p>
            <RepeatList
              mode={mode}
              tag="ul"
              className="flex flex-wrap gap-1"
              alias="tag"
              collection="stage.tags"
              items={stage.tags}
              placeholder="tag"
            >
              {(tag) => (
                <li className="badge badge-sm badge-outline">{slot(mode, "tag", tag)}</li>
              )}
            </RepeatList>
            <a
              className="btn btn-sm btn-primary mt-2"
              href={
                "/tasks?track=" + slot(mode, "track.id", "t") + "&stage=" + slot(mode, "stage.id", "s")
              }
            >
              {slot(mode, "start_label", data.start_label)}
            </a>
          </div>
        </li>
      )}
    </RepeatList>
  );
}
