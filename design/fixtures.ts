// The data a page is designed against. Deliberately realistic: a Thai title beside an English one, a stage
// with no Thai title at all — the E6.1 falsification has to be visible in the gallery, not only in the test.
export interface StageFixture {
  id: string;
  title_en: string;
  title_th: string;
  difficulty_min: number;
  difficulty_max: number;
  tags: string[];
}

export interface TrackFixture {
  id: string;
  title_en: string;
  title_th: string;
  description_en: string;
  description_th: string;
  stages: StageFixture[];
}

export interface PageFixture {
  site_name: string;
  heading: string;
  lede: string;
  band_label: string;
  start_label: string;
  empty_label: string;
  tracks: TrackFixture[];
}

export const homeFixture: PageFixture = {
  site_name: "CMS Learn",
  heading: "หลักสูตร",
  lede: "เลือกเส้นทางและเริ่มจากขั้นที่ตรงกับคุณ",
  band_label: "ระดับความยาก",
  start_label: "เริ่มขั้นนี้",
  empty_label: "ยังไม่มีหลักสูตร",
  tracks: [
    {
      id: "t-usaco",
      title_en: "USACO ladder",
      title_th: "เส้นทางแบบ USACO",
      description_en: "Four divisions, the way USACO itself progresses.",
      description_th: "สี่ระดับตามแนวทางของ USACO",
      stages: [
        {
          id: "st-bronze",
          title_en: "Bronze",
          title_th: "ทองแดง",
          difficulty_min: 800,
          difficulty_max: 1200,
          tags: ["io", "complexity", "simulation"],
        },
        {
          // No Thai title on purpose: the page must fall back to English rather than render an empty
          // heading, and the gallery shows whether it does.
          id: "st-silver",
          title_en: "Silver",
          title_th: "",
          difficulty_min: 1100,
          difficulty_max: 1500,
          tags: ["prefix-sums", "binary-search"],
        },
      ],
    },
  ],
};
