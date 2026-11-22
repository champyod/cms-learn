import type { FalsifyCase, Gate } from "../contract.ts";

const red = (message: string): FalsifyCase["expect"] => ({ status: "non-zero", message, messageMatch: "case-insensitive" });

const HARDENING_BLOCK =
  "  security_opt:\n    - no-new-privileges:true\n  logging:\n    driver: json-file\n    options:\n      max-size: \"10m\"\n      max-file: \"3\"\n";

const JUDGE_CAPABILITIES =
  "    cap_add:\n      - SYS_ADMIN\n      - SYS_CHROOT\n      - SETUID\n      - SETGID\n      - CHOWN\n      - DAC_OVERRIDE\n      - FOWNER\n      - NET_ADMIN\n";

export const DEPLOY_GATE: Gate = {
  id: "deploy",
  stage: ["deploy"],
  command: { command: "bash", args: ["{repo}/tools/check-deploy.sh"], env: { DEPLOY_DIR: "{root}/deploy" } },
  cases: [
    {
      // The stack must refuse without the object store's credential: with the compose accepting an empty
      // value again, the store comes up allow-all and only the network stands in the way.
      name: "store-credential-not-required",
      plant: [
        {
          kind: "run",
          script:
            "sed -i 's|${CMSLEARN_S3_SECRET_ACCESS_KEY:?set CMSLEARN_S3_SECRET_ACCESS_KEY in .env}|${CMSLEARN_S3_SECRET_ACCESS_KEY:-}|' deploy/docker-compose.yml",
          verify: "grep -q 'CMSLEARN_S3_SECRET_ACCESS_KEY:-' deploy/docker-compose.yml",
        },
      ],
      expect: red("does not require CMSLEARN_S3_SECRET_ACCESS_KEY"),
    },
    {
      // One command is the deployment's entry point: without it the operator is back at the README's
      // eight steps, and the contract has nothing to assert.
      name: "first-run-target-removed",
      command: {
        command: "bash",
        args: ["{repo}/tools/check-deploy.sh"],
        env: { DEPLOY_DIR: "{root}/deploy", FIRST_RUN_MAKEFILE: "{root}/Makefile" },
      },
      plant: [{ kind: "write", path: "Makefile", content: "help:\n\t@printf 'no first run\\n'\n" }],
      expect: red("declares no first-run target"),
    },
    {
      // A built image without a build block: compose build exits 0 and produces nothing, and the run then
      // pulls an image that was never made. The gate takes the built set from release.toml, not a hand list.
      name: "built-service-without-build",
      plant: [
        { kind: "patch", path: "deploy/docker-compose.yml", find: "    build:\n      context: ./schema\n", replace: "" },
      ],
      expect: red("without a build section"),
    },
    {
      name: "db-published-publicly",
      plant: [{ kind: "patch", path: "deploy/docker-compose.yml", find: "127.0.0.1:5432:5432", replace: "5432:5432" }],
      expect: red("only proxy may face the internet"),
    },
    {
      name: "judge-with-egress",
      plant: [{ kind: "replaceAll", path: "deploy/docker-compose.yml", find: "networks: [internal]", replace: "networks: [edge]", matches: 6 }],
      expect: red("could reach the internet"),
    },
    {
      name: "unpinned-image",
      plant: [
        {
          kind: "run",
          script: "sed -i 's|image: postgres:18.6@sha256:[0-9a-f]*|image: postgres:latest|' deploy/docker-compose.yml",
          verify: "grep -q 'image: postgres:latest' deploy/docker-compose.yml",
        },
      ],
      expect: red("not pinned"),
    },
    {
      name: "no-new-privileges-dropped",
      plant: [{ kind: "patch", path: "deploy/docker-compose.yml", find: HARDENING_BLOCK, replace: "  logging:\n    driver: json-file\n" }],
      expect: red("no-new-privileges"),
    },
    {
      // The judge's security_opt replaces the anchor's list, so its own no-new-privileges line is the only
      // thing that keeps the sandbox from gaining privileges it did not start with.
      name: "judge-no-new-privileges-dropped",
      plant: [
        { kind: "patch", path: "deploy/docker-compose.yml", find: "      - no-new-privileges:true\n      - seccomp:unconfined\n", replace: "      - seccomp:unconfined\n" },
      ],
      expect: red("judge does not set no-new-privileges:true"),
    },
    {
      name: "read-only-rootfs-dropped",
      plant: [{ kind: "patch", path: "deploy/docker-compose.yml", find: "  read_only: true\n  cap_drop: [ALL]\n", replace: "  cap_drop: [ALL]\n" }],
      expect: red("read_only: true is the rule"),
    },
    {
      name: "capabilities-not-dropped",
      plant: [{ kind: "patch", path: "deploy/docker-compose.yml", find: "  cap_drop: [ALL]\n", replace: "" }],
      expect: red("does not drop every capability"),
    },
    {
      name: "service-adds-an-unneeded-capability",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: "  api:\n    <<: *hardening\n    build:",
          replace: "  api:\n    <<: *hardening\n    cap_add: [SYS_ADMIN]\n    build:",
        },
      ],
      expect: red("api adds back SYS_ADMIN"),
    },
    {
      name: "judge-loses-a-capability-its-sandbox-needs",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: JUDGE_CAPABILITIES,
          replace: JUDGE_CAPABILITIES.replace("      - SYS_ADMIN\n", ""),
        },
      ],
      expect: red("judge adds back"),
    },
    {
      name: "process-limit-dropped",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: "          cpus: \"0.5\"\n          memory: 512m\n          pids: 256\n",
          replace: "          cpus: \"0.5\"\n          memory: 512m\n",
        },
      ],
      expect: red("objects sets no process limit"),
    },
    {
      name: "memory-limit-dropped",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: "          cpus: \"0.1\"\n          memory: 64m\n          pids: 64\n",
          replace: "          cpus: \"0.1\"\n          pids: 64\n",
        },
      ],
      expect: red("node-exporter sets no memory limit"),
    },
    {
      name: "cpu-limit-dropped",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: "          cpus: \"0.2\"\n          memory: 128m\n          pids: 64\n",
          replace: "          memory: 128m\n          pids: 64\n",
        },
      ],
      expect: red("backup sets no CPU limit"),
    },
    {
      name: "judge-writable-path-not-named",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: "      - /var/lib/isolate:rw,exec,nosuid,nodev,size=512m\n",
          replace: "",
        },
      ],
      expect: red("judge's writable paths are"),
    },
    {
      name: "bind-mount-writable",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: "      - ./host.env:/run/cms-learn/host.env:ro",
          replace: "      - ./host.env:/run/cms-learn/host.env:rw",
        },
      ],
      expect: red("api's writable paths are"),
    },
    {
      // A file capability outside the container's bounding set makes exec fail, so the image that drops
      // every capability must not carry one on the binary.
      name: "proxy-binary-carries-a-file-capability",
      plant: [
        {
          kind: "patch",
          path: "deploy/proxy/Dockerfile",
          find: "USER 10001:10001\n",
          replace: "RUN setcap cap_net_bind_service=+ep /usr/bin/caddy\n\nUSER 10001:10001\n",
        },
      ],
      expect: red("the proxy binary carries file capabilities"),
    },
    {
      name: "internal-network-gateway",
      plant: [{ kind: "patch", path: "deploy/docker-compose.yml", find: "  internal:\n    internal: true", replace: "  internal:" }],
      expect: red("not marked internal"),
    },
    {
      name: "caddy-directive-unordered",
      plant: [{ kind: "patch", path: "deploy/Caddyfile", find: "\torder crowdsec first\n", replace: "" }],
      expect: red("Caddyfile is not valid"),
    },
    {
      name: "proxy-port-not-https",
      plant: [{ kind: "patch", path: "deploy/docker-compose.yml", find: "      - \"80:8080\"", replace: "      - \"8080:8080\"" }],
      expect: red("unexpected port"),
    },
    {
      name: "panel-route-removed",
      plant: [{ kind: "patch", path: "deploy/Caddyfile", find: "\thandle @panel {\n\t\treverse_proxy api:8081\n\t}\n", replace: "" }],
      expect: red("/admin is not routed to the panel listener"),
    },
    {
      name: "panel-route-to-the-public-listener",
      plant: [{ kind: "patch", path: "deploy/Caddyfile", find: "\t\treverse_proxy api:8081", replace: "\t\treverse_proxy api:8080" }],
      expect: red("/admin is not routed to the panel listener"),
    },
    {
      name: "api-mounts-the-checkout",
      plant: [
        {
          kind: "patch",
          path: "deploy/docker-compose.yml",
          find: "      - ./host.env:/run/cms-learn/host.env:ro",
          replace: "      - ./host.env:/run/cms-learn/host.env:ro\n      - ../api/templates:/src/api/templates:ro",
        },
      ],
      expect: red("the image must carry what it renders"),
    },
  ],
};
