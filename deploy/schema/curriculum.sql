-- The curriculum. A track is an ordered view over the same module set: it decides sequence and
-- difficulty band, never content, and a task never belongs to a track. Nothing here names a specific
-- track in code — the reader walks whatever rows exist, so a third route is a data change.

CREATE TABLE tracks (
  id text PRIMARY KEY,
  position integer NOT NULL,
  title_en text NOT NULL CHECK (length(btrim(title_en)) > 0),
  title_th text NOT NULL CHECK (length(btrim(title_th)) > 0),
  description_en text NOT NULL CHECK (length(btrim(description_en)) > 0),
  description_th text NOT NULL CHECK (length(btrim(description_th)) > 0),
  content_hash text NOT NULL,
  seeded boolean NOT NULL DEFAULT true,
  CONSTRAINT tracks_id_shape CHECK (id ~ '^t-[a-z0-9][a-z0-9-]{0,62}$'),
  CONSTRAINT tracks_position_unique UNIQUE (position)
);

CREATE TABLE track_stages (
  track_id text NOT NULL,
  id text NOT NULL,
  position integer NOT NULL,
  title_en text NOT NULL CHECK (length(btrim(title_en)) > 0),
  title_th text NOT NULL CHECK (length(btrim(title_th)) > 0),
  difficulty_min integer NOT NULL,
  difficulty_max integer NOT NULL,
  CONSTRAINT track_stages_pkey PRIMARY KEY (track_id, id),
  CONSTRAINT track_stages_track_fkey FOREIGN KEY (track_id) REFERENCES tracks (id) ON DELETE CASCADE,
  CONSTRAINT track_stages_position_unique UNIQUE (track_id, position),
  CONSTRAINT track_stages_band CHECK (difficulty_min <= difficulty_max),
  CONSTRAINT track_stages_difficulty_band CHECK (
    difficulty_min BETWEEN 800 AND 3500
    AND difficulty_max BETWEEN 800 AND 3500
    AND difficulty_min % 100 = 0
    AND difficulty_max % 100 = 0
  ),
  CONSTRAINT track_stages_id_shape CHECK (id ~ '^st-[a-z0-9][a-z0-9-]{0,62}$')
);

-- A stage selects by tag, so an unknown tag is a stage that silently matches nothing. The foreign key
-- makes the vocabulary the only source of tags, and the seeder checks it before touching the database.
CREATE TABLE stage_tags (
  track_id text NOT NULL,
  stage_id text NOT NULL,
  tag text NOT NULL REFERENCES topics (id),
  CONSTRAINT stage_tags_pkey PRIMARY KEY (track_id, stage_id, tag),
  CONSTRAINT stage_tags_stage_fkey FOREIGN KEY (track_id, stage_id)
    REFERENCES track_stages (track_id, id) ON DELETE CASCADE
);

CREATE INDEX stage_tags_tag_idx ON stage_tags (tag);
