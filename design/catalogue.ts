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
  settings: SettingEntry[];
}

const cataloguePath = join(import.meta.dir, "..", "deploy", "catalogue", "settings.json");
const catalogue = JSON.parse(readFileSync(cataloguePath, "utf8")) as { settings: SettingEntry[] };

export const settingsFixture: SettingsFixture = {
  site_name: "CMS Learn",
  title: "ตั้งค่า",
  lede: "ค่าที่ระบบอ่านจริง แต่ละรายการมีชั้นการเข้าถึงกำกับ",
  submit_label: "บันทึก",
  settings: catalogue.settings,
};
