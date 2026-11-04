import type { Gate, GateCommand } from "../contract.ts";

// An empty TRIVY_IMAGE means unset, so the fallback is || and not ??.
const TRIVY_IMAGE = process.env.TRIVY_IMAGE || "aquasec/trivy:0.66.0";

const SECRETS_SCAN: GateCommand = {
  command: "docker",
  args: ["run", "--rm", "-v", "{root}:/src:ro", TRIVY_IMAGE, "fs", "--quiet", "--scanners", "secret", "--exit-code", "1", "/src"],
};

const CONFIG_SCAN: GateCommand = {
  command: "docker",
  args: ["run", "--rm", "-v", "{root}:/src:ro", TRIVY_IMAGE, "config", "--quiet", "--exit-code", "1", "--severity", "MEDIUM,HIGH,CRITICAL", "/src"],
};

export const SECURITY_GATE: Gate = {
  id: "security",
  stage: [],
  command: SECRETS_SCAN,
  cases: [
    {
      name: "planted-github-token",
      plant: [
        {
          kind: "run",
          script: `token="$(tr -dc 'A-Za-z0-9' < /dev/urandom | head -c 36)" && printf 'token = "ghp_%s"\\n' "$token" > config.py`,
          verify: `grep -Eq '^token = "ghp_[A-Za-z0-9]{36}"$' config.py`,
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "planted-private-key",
      plant: [
        {
          kind: "run",
          script: `echo '-----BEGIN RSA PRIVATE KEY-----' > id_rsa && head -c 64 /dev/urandom | base64 >> id_rsa && echo '-----END RSA PRIVATE KEY-----' >> id_rsa`,
          verify: `grep -q -- '-----BEGIN RSA PRIVATE KEY-----' id_rsa && grep -q -- '-----END RSA PRIVATE KEY-----' id_rsa`,
        },
      ],
      expect: { status: "non-zero" },
    },
    {
      name: "root-user-dockerfile",
      plant: [{ kind: "write", path: "Dockerfile", content: "FROM alpine:3.21\nRUN echo hi\nCMD [\"sh\"]\n" }],
      command: CONFIG_SCAN,
      expect: { status: "non-zero" },
    },
    {
      name: "clean-dockerfile",
      plant: [
        {
          kind: "write",
          path: "Dockerfile",
          content: "FROM alpine:3.21\nRUN adduser -D app\nUSER app\nHEALTHCHECK CMD true\nCMD [\"sh\"]\n",
        },
      ],
      command: CONFIG_SCAN,
      expect: { status: "zero" },
    },
  ],
};
