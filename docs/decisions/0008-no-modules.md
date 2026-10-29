# 0008 — The curriculum is tags and routes, with no module layer

**Status:** accepted · **Date:** 2026-10-29 · **Numbering:** 0007 is claimed by the tag-vocabulary lane, so this record takes 0008

## Context

The content model had three layers: a topic vocabulary, a set of 28 modules (each in one of 11 areas,
each carrying one to three topics and a prerequisite list), and two tracks whose stages named modules
and a difficulty band.

The operator's own description of the system has one unit: a student picks tags, gets served a problem
for them, and the generator writes more when the bank runs out. Measured against that, a module is a
second name for a set of tags — 17 of the 28 modules covered two tags, and 5 had a title identical to
their only tag. Two names for one thing drift, and the prerequisite graph the modules carried was a
third ordering next to the track's stages.

## Decision

Delete the module layer. A stage of a route names the tags it teaches and the difficulty band they are
served at. A tag may appear in several stages of a route, because a skill is met again at a harder
band; it may not appear twice inside one stage.

The gate keeps the properties that mattered: every tag a stage names exists, every tag in the
vocabulary is placed by some stage, no band is inverted, and a task is servable only when some stage's
band contains its difficulty and names one of its tags.

## Consequences

- `content/modules.json` and its schema are gone; the 33 tags the modules referenced are now placed on
  the stages directly, so nothing needed to be dropped.
- Areas are gone with the modules. Browsing by area was the only thing they supported.
- The prerequisite graph is gone as an object. Ordering now comes from the route's stage sequence, which
  is linear and therefore cannot have a cycle; the cycle check that guarded modules has no subject.
- Four falsification cases that planted module defects were rewritten as their tag equivalents rather
  than deleted: unknown tag, tag twice in a stage, stage with no tag, and unplaced tag. Eleven cases
  behave on the new model.
