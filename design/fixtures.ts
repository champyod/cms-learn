// The data a page is designed against. Deliberately realistic: a Thai title beside an English one, a stage
// with no Thai title at all — the E6.1 falsification has to be visible in the gallery, not only in the test.
export interface StageFixture {
  id: string;
  title_en: string;
  title_th: string;
  difficulty_min: number;
  difficulty_max: number;
  tags: string[];
  /// How much of the stage a learner has done, and whether the bank offers a next task.
  done: number;
  total: number;
  has_next: boolean;
  next_id: string;
  next_title: string;
  is_finished: boolean;
  is_empty: boolean;
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
  done_label: string;
  next_label: string;
  finished_label: string;
  no_next_label: string;
  empty_stage_label: string;
  has_learner: boolean;
  tracks: TrackFixture[];
}

export interface TaskFixture {
  site_name: string;
  task_id: string;
  title: string;
  limit_label: string;
  used_memory_label: string;
  reserved_memory_label: string;
  time_ms: number;
  memory_used_mb: number;
  memory_reserved_mb: number;
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
  used_memory_label: "ใช้จริง",
  reserved_memory_label: "ที่จองไว้",
  time_ms: 1000,
  memory_used_mb: 256,
  memory_reserved_mb: 512,
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
  done_label: "ทำแล้ว",
  next_label: "แบบฝึกหัดถัดไป",
  finished_label: "ขั้นนี้ทำครบแล้ว",
  no_next_label: "ยังไม่มีแบบฝึกหัดใหม่ในขั้นนี้ตอนนี้",
  empty_stage_label: "ขั้นนี้ยังไม่มีแบบฝึกหัด",
  has_learner: true,
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
          done: 1,
          total: 3,
          has_next: true,
          next_id: "range-sum-query",
          next_title: "ผลรวมของช่วงข้อมูล",
          is_finished: false,
          is_empty: false,
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
          done: 2,
          total: 2,
          has_next: false,
          next_id: "",
          next_title: "",
          is_finished: true,
          is_empty: false,
        },
      ],
    },
  ],
};

/// A row of the all-tasks page: what the list shows, nothing it does not.
export interface TaskListRow {
  id: string;
  title: string;
  difficulty: number;
}

/// One entry of the topic vocabulary the filter offers.
export interface TopicChoice {
  id: string;
  label: string;
}

/// One rung as the page filter offers it: the same shape the shell's meter draws.
export interface LevelChoice {
  id: string;
  title: string;
  band: string;
  value: string;
  is_chosen: boolean;
}

/// The all-tasks page: two filters, the words they need, and the sentence an empty result shows.
export interface TaskListFixture {
  site_name: string;
  heading: string;
  lede: string;
  level_label: string;
  any_level_label: string;
  topics_label: string;
  apply_label: string;
  current_label: string;
  difficulty_label: string;
  empty_label: string;
  stage_note: string;
  topics_line: string;
  filter_level_name: string;
  topics: TopicChoice[];
  levels: LevelChoice[];
  tasks: TaskListRow[];
}

export const taskListFixture: TaskListFixture = {
  site_name: "CMS Learn",
  heading: "แบบฝึกหัดทั้งหมด",
  lede: "กรองตามระดับและหัวข้อ เลือกได้หลายหัวข้อพร้อมกัน",
  level_label: "ระดับ",
  any_level_label: "ทุกระดับ",
  topics_label: "หัวข้อ",
  apply_label: "กรอง",
  current_label: "กำลังกรอง",
  difficulty_label: "ความยาก",
  empty_label: "ไม่มีแบบฝึกหัดที่ตรงกับตัวกรองนี้",
  stage_note: "ขั้น: ทองแดง",
  topics_line: "io, complexity",
  filter_level_name: "ค่าย 1",
  levels: [
    { id: "toi-zero", title: "TOI-Zero", band: "800–1400", value: "800-1400", is_chosen: false },
    { id: "camp-1", title: "Camp 1", band: "1500–1800", value: "1500-1800", is_chosen: true },
    { id: "camp-2", title: "Camp 2", band: "1900–2400", value: "1900-2400", is_chosen: false },
    { id: "toi-ioi", title: "TOI-IOI", band: "2500–3500", value: "2500-3500", is_chosen: false },
  ],
  topics: [
    { id: "complexity", label: "ความซับซ้อน" },
    { id: "io", label: "การรับเข้าและส่งออก" },
  ],
  tasks: [
    { id: "range-sum-query", title: "ผลรวมของช่วงข้อมูล", difficulty: 900 },
    { id: "two-numbers", title: "ผลรวมสองจำนวน", difficulty: 800 },
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
  grant_role_label: string;
  role_name_label: string;
  roles: RoleFixture[];
  administrators: AdminFixtureRow[];
  submit_label: string;
  learners: LearnerRow[];
}

/// One role the grant form can name, and one administrator it can name.
export interface RoleFixture {
  id: string;
  name: string;
}

export interface AdminFixtureRow {
  id: string;
  handle: string;
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
  grant_role_label: "ให้บทบาท",
  role_name_label: "ชื่อบทบาท",
  roles: [
    { id: "6f1c2a94-0b7e-4c53-9a1d-2e8f7b6c5d40", name: "operator" },
    { id: "7a2d3b05-1c8f-4d64-ab2e-3f9a8c7d6e51", name: "author" },
  ],
  administrators: [
    { id: "9b3e4c16-2d9a-4e75-bc3f-4a0b9d8e7f62", handle: "operator" },
  ],
  submit_label: "บันทึก",
  learners: [
    { id: "8f14e45f-ceea-467a-9c1e-0f5a2b3c4d5e", handle: "student_one", status: "active", level: "camp-1" },
    { id: "c9f0f895-fb98-4c4b-8b0d-1a2b3c4d5e6f", handle: "student_two", status: "pending", level: "" },
  ],
};
