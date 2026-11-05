-- The level list: bands over the one difficulty scale, with a name per language and an icon.
--
-- A level is never stored on a task. A task keeps its difficulty and its level is read from the band
-- that contains it, so editing a range renames nothing and migrates nothing. A principal stores the
-- level id, which survives a rename because it points at the row rather than at a number.

CREATE TABLE levels (
  id text PRIMARY KEY,
  position integer NOT NULL,
  difficulty_min integer NOT NULL,
  difficulty_max integer NOT NULL,
  icon text NOT NULL,
  description_en text NOT NULL DEFAULT '',
  CONSTRAINT levels_id_shape CHECK (id ~ '^[a-z0-9][a-z0-9-]{0,62}$'),
  CONSTRAINT levels_position_unique UNIQUE (position),
  CONSTRAINT levels_band CHECK (difficulty_min <= difficulty_max),
  CONSTRAINT levels_difficulty_band CHECK (
    difficulty_min BETWEEN 800 AND 3500
    AND difficulty_max BETWEEN 800 AND 3500
    AND difficulty_min % 100 = 0
    AND difficulty_max % 100 = 0
  ),
  CONSTRAINT levels_icon_shape CHECK (icon ~ '^[a-z0-9][a-z0-9-]{0,62}$')
);

-- The only supported ordering of the list, so a reader never repeats the comparison.
CREATE UNIQUE INDEX levels_order_idx ON levels (difficulty_min, difficulty_max);

CREATE TABLE level_titles (
  level_id text NOT NULL REFERENCES levels (id) ON DELETE CASCADE,
  language text NOT NULL,
  title text NOT NULL CHECK (length(btrim(title)) > 0),
  PRIMARY KEY (level_id, language),
  CONSTRAINT level_titles_language_shape CHECK (language ~ '^[a-z]{2}(-[A-Za-z0-9]{2,8})*$')
);

-- The bands have to partition the scale: no gap leaves a difficulty with no level, and no overlap lets
-- one difficulty carry two. Checked over the whole table rather than per row, so the list is validated
-- as a list, once, whichever row changed.
CREATE FUNCTION levels_partition_the_scale() RETURNS trigger AS $$
DECLARE
  expected integer := 800;
  current_min integer;
BEGIN
  FOR current_min IN SELECT difficulty_min FROM levels ORDER BY difficulty_min LOOP
    IF current_min <> expected THEN
      RAISE EXCEPTION 'level list is not a partition of 800..3500: a band starts at %, expected %',
        current_min, expected;
    END IF;
    expected := (SELECT difficulty_max + 100 FROM levels WHERE difficulty_min = current_min);
  END LOOP;
  IF expected - 100 <> 3500 THEN
    RAISE EXCEPTION 'level list is not a partition of 800..3500: the last band ends at %, expected 3500',
      expected - 100;
  END IF;
  RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE CONSTRAINT TRIGGER levels_partition_check
  AFTER INSERT OR UPDATE OR DELETE ON levels
  DEFERRABLE INITIALLY DEFERRED
  FOR EACH ROW EXECUTE FUNCTION levels_partition_the_scale();
