import type { Gate, Plant } from "../contract.ts";

/// A clean fixture for one technique: `dp`, whose id, English label, Thai label and acronym are four spellings of
/// the same answer. The leak cases put one of them in a title or a statement.
const CLEAN: Readonly<Record<string, string>> = {
  "locales.txt": "en\nth\n",
  "technique.txt": "dp\n",
  "title.en.txt": "Counting the ways\n",
  "title.th.txt": "การนับจำนวนวิธี\n",
  "statement.en.txt": "# Counting the ways\n\nYou are given n and must print the number of ways.\n",
  "statement.th.txt": "# การนับจำนวนวิธี\n\nกำหนดค่า n แล้วพิมพ์จำนวนวิธีทั้งหมด\n",
};

const fixture = (directory: string, overrides: Readonly<Record<string, string>> = {}): Plant[] =>
  Object.entries({ ...CLEAN, ...overrides }).map(([name, content]) => ({
    kind: "write",
    path: `${directory}/${name}`,
    content,
  }));

const filesAt = (directory: string, files: Readonly<Record<string, string>>): Plant[] =>
  Object.entries(files).map(([name, content]) => ({ kind: "write", path: `${directory}/${name}`, content }));

const englishOnly = (directory: string, overrides: Readonly<Record<string, string>> = {}): Plant[] => {
  const files: Record<string, string> = { ...CLEAN, ...overrides, "locales.txt": "en\n" };
  delete files["title.th.txt"];
  delete files["statement.th.txt"];
  return filesAt(directory, files);
};

const englishAndFrench = (directory: string, overrides: Readonly<Record<string, string>> = {}): Plant[] => {
  const files: Record<string, string> = { ...CLEAN, "locales.txt": "en\nfr\n" };
  delete files["title.th.txt"];
  delete files["statement.th.txt"];
  files["title.fr.txt"] = "Compter les façons\n";
  files["statement.fr.txt"] = "# Compter les façons\n\nOn vous donne n et le nombre de façons à imprimer.\n";
  Object.assign(files, overrides);
  return filesAt(directory, files);
};

const command = (directory: string) => ({
  command: "bash",
  args: ["tools/check-algo-leak.sh", directory],
});

/// The algorithm-leak gate's own fixtures run with no argument; a directory argument compares that fixture. The
/// labels come from content/topics.json, so the plant names a technique the vocabulary defines.
export const ALGO_LEAK_GATE: Gate = {
  id: "algo-leak",
  stage: ["content", "core", "docs/dependencies.md", "generator", "tools"],
  command: { command: "bash", args: ["tools/check-algo-leak.sh"] },
  cases: [
    { name: "self-check", expect: { status: "zero" } },
    {
      name: "clean-control",
      plant: fixture("fixtures/algo-clean"),
      command: command("fixtures/algo-clean"),
      expect: { status: "zero" },
    },
    {
      // English-only ships, so the gate must pass it without a second language to lean on.
      name: "english-only-passes",
      plant: englishOnly("fixtures/algo-english"),
      command: command("fixtures/algo-english"),
      expect: { status: "zero" },
    },
    {
      // A third language is matched by the same names as any other.
      name: "french-statement-names-the-label",
      plant: englishAndFrench("fixtures/algo-french", {
        "statement.fr.txt": "# Compter les façons\n\nOn construit une table de Dynamic programming.\n",
      }),
      command: command("fixtures/algo-french"),
      expect: { status: "non-zero", message: "names the intended approach" },
    },
    {
      // The English label: the spelling a writer reaches for first.
      name: "label-named-in-the-title",
      plant: fixture("fixtures/algo-label", { "title.en.txt": "Dynamic Programming on a Grid\n" }),
      command: command("fixtures/algo-label"),
      expect: { status: "non-zero", message: "names the intended approach" },
    },
    {
      // The topic id: the same answer without the words the label uses.
      name: "id-named-in-the-statement",
      plant: fixture("fixtures/algo-id", {
        "statement.en.txt": "# Counting the ways\n\nBuild a dp table over the values and add it up.\n",
      }),
      command: command("fixtures/algo-id"),
      expect: { status: "non-zero", message: "names the intended approach" },
    },
    {
      // The acronym is derived from the English label, so a technique whose id is a name still matches the
      // abbreviation a writer would use instead of it.
      name: "derived-acronym-named-in-the-statement",
      plant: fixture("fixtures/algo-acronym", {
        "technique.txt": "dijkstra\n",
        "title.en.txt": "A grid\n",
        "statement.en.txt": "# A grid\n\nBuild an sp table over the cells.\n",
        "title.th.txt": "ตาราง\n",
        "statement.th.txt": "# ตาราง\n\nเดินในตารางจนถึงมุมขวาล่าง\n",
      }),
      command: command("fixtures/algo-acronym"),
      expect: { status: "non-zero", message: "names the intended approach" },
    },
    {
      // The Thai label: the language most students read, and the one a segmenter-free check could miss.
      name: "thai-label-named-in-the-statement",
      plant: fixture("fixtures/algo-thai", {
        "statement.th.txt": "# การนับจำนวนวิธี\n\nใช้การโปรแกรมพลวัตกับค่าที่กำหนด\n",
      }),
      command: command("fixtures/algo-thai"),
      expect: { status: "non-zero", message: "names the intended approach" },
    },
  ],
};
