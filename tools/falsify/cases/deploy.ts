import type { FalsifyCase, Gate } from "../contract.ts";

const red = (message: string): FalsifyCase["expect"] => ({ status: "non-zero", message, messageMatch: "case-insensitive" });

const HARDENING_BLOCK =
  "  security_opt:\n    - no-new-privileges:true\n  logging:\n    driver: json-file\n    options:\n      max-size: \"10m\"\n      max-file: \"3\"\n";

export const DEPLOY_GATE: Gate = {
  id: "deploy",
  stage: ["deploy"],
  command: { command: "bash", args: ["{repo}/tools/check-deploy.sh"], env: { DEPLOY_DIR: "{root}/deploy" } },
  cases: [
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
