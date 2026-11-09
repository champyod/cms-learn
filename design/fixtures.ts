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

export interface TaskFixture {
  site_name: string;
  task_id: string;
  title: string;
  limit_label: string;
  time_ms: number;
  memory_mb: number;
  statement: string;
  example_label: string;
  example_input: string;
  example_output: string;
  source_label: string;
  submit_label: string;
  starter_source: string;
  verdict_pending: string;
}

export const taskFixture: TaskFixture = {
  site_name: "CMS Learn",
  task_id: "range-sum-query",
  title: "ผลรวมของช่วงข้อมูล",
  limit_label: "ขีดจำกัด",
  time_ms: 1000,
  memory_mb: 256,
  statement: "<p>ให้ลำดับตัวเลขและคำถามหลายข้อ จงตอบผลรวมของแต่ละช่วง</p>",
  example_label: "ตัวอย่าง",
  example_input: "3 2\n1 2 3\n1 2\n2 3\n",
  example_output: "3\n5\n",
  source_label: "โค้ดของคุณ",
  submit_label: "ส่งคำตอบ",
  starter_source: "#include <iostream>\nint main() { return 0; }\n",
  verdict_pending: "ยังไม่มีผลการตรวจ",
};

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

export interface SigninFixture {
  site_name: string;
  title: string;
  lede: string;
  email_label: string;
  password_label: string;
  submit_label: string;
  signup_label: string;
  /// The canned message a failed attempt or a completed verification shows; empty on a first visit.
  message: string;
}

export interface SignupFixture {
  site_name: string;
  title: string;
  lede: string;
  email_label: string;
  handle_label: string;
  password_label: string;
  submit_label: string;
  signin_label: string;
  message: string;
}

export const signinFixture: SigninFixture = {
  site_name: "CMS Learn",
  title: "เข้าสู่ระบบ",
  lede: "ใช้บัญชีที่ยืนยันอีเมลแล้วเพื่อเข้าสู่ระบบ",
  email_label: "อีเมล",
  password_label: "รหัสผ่าน",
  submit_label: "เข้าสู่ระบบ",
  signup_label: "ยังไม่มีบัญชี? สมัครสมาชิก",
  message: "",
};

export const signupFixture: SignupFixture = {
  site_name: "CMS Learn",
  title: "สมัครสมาชิก",
  lede: "สร้างบัญชี แล้วยืนยันอีเมลของคุณก่อนเข้าสู่ระบบ",
  email_label: "อีเมล",
  handle_label: "ชื่อผู้ใช้",
  password_label: "รหัสผ่าน",
  submit_label: "สมัครสมาชิก",
  signin_label: "มีบัญชีอยู่แล้ว? เข้าสู่ระบบ",
  message: "",
};

/// A row of the administrator panel's roster.
export interface LearnerRow {
  id: string;
  handle: string;
  status: string;
  level: string;
}

/// The administrator panel's page data. Every field names a template variable the server fills.
export interface AdminFixture {
  site_name: string;
  title: string;
  lede: string;
  admin_handle: string;
  csrf_token: string;
  notice: string;
  message: string;
  roster_label: string;
  empty_label: string;
  id_label: string;
  handle_label: string;
  status_label: string;
  level_label: string;
  email_label: string;
  password_label: string;
  role_label: string;
  learner_id_label: string;
  admin_id_label: string;
  create_learner_label: string;
  create_admin_label: string;
  approve_label: string;
  revoke_admin_label: string;
  issue_verification_label: string;
  submit_label: string;
  learners: LearnerRow[];
}

export const adminFixture: AdminFixture = {
  site_name: "CMS Learn",
  title: "ผู้ดูแลระบบ",
  lede: "ลงชื่อเข้าใช้ในชื่อ",
  admin_handle: "operator",
  csrf_token: "0f8c1a5e7b9d4c2a",
  notice: "สร้างบัญชีผู้เรียนแล้ว",
  message: "",
  roster_label: "บัญชีผู้เรียน",
  empty_label: "ยังไม่มีผู้เรียน",
  id_label: "รหัสบัญชี",
  handle_label: "ชื่อบัญชี",
  status_label: "สถานะ",
  level_label: "ระดับ",
  email_label: "อีเมล",
  password_label: "รหัสผ่าน",
  role_label: "บทบาท",
  learner_id_label: "รหัสผู้เรียน",
  admin_id_label: "รหัสผู้ดูแล",
  create_learner_label: "สร้างบัญชีผู้เรียน",
  create_admin_label: "สร้างบัญชีผู้ดูแล",
  approve_label: "อนุมัติผู้เรียน",
  revoke_admin_label: "เพิกถอนผู้ดูแล",
  issue_verification_label: "ออกรหัสยืนยัน",
  submit_label: "บันทึก",
  learners: [
    { id: "8f14e45f-ceea-467a-9c1e-0f5a2b3c4d5e", handle: "student_one", status: "active", level: "camp-1" },
    { id: "c9f0f895-fb98-4c4b-8b0d-1a2b3c4d5e6f", handle: "student_two", status: "pending", level: "" },
  ],
};
