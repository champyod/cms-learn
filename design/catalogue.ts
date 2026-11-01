import { readFileSync } from "node:fs";
import { join } from "node:path";

// The settings catalogue is the source of the form: the page renders whatever this file declares, so a key
// enters the UI only by being in the catalogue, and a tier is enforced by the same declaration.
export type SettingKind = "bool" | "int" | "text";
export type SettingTier = "public" | "operator" | "secret";

export interface SettingEntry {
  key: string;
  kind: SettingKind;
  tier: SettingTier;
  default: string;
  reload: string;
}

export interface SettingsFixture {
  site_name: string;
  title: string;
  lede: string;
  submit_label: string;
  /// The group this request renders. The page shows one group at a time, so a request names the one it wants.
  group: string;
  settings: SettingEntry[];
}

/// A setting key's first segment is its group: `mail.password` belongs to `mail`.
export function groupOf(key: string): string {
  const separator = key.indexOf(".");
  return separator === -1 ? key : key.slice(0, separator);
}

const cataloguePath = join(import.meta.dir, "..", "deploy", "catalogue", "settings.json");
const catalogue = JSON.parse(readFileSync(cataloguePath, "utf8")) as { settings: SettingEntry[] };

export const settingsFixture: SettingsFixture = {
  site_name: "CMS Learn",
  title: "ตั้งค่า",
  lede: "ค่าที่ระบบอ่านจริง แต่ละรายการมีชั้นการเข้าถึงกำกับ",
  submit_label: "บันทึก",
  group: groupOf(catalogue.settings[0]?.key ?? ""),
  settings: catalogue.settings,
};
