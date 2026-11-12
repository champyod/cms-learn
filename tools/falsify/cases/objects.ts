import type { Gate } from "../contract.ts";

/// The objects gate: one client, a missing object refused by name, and a deployment that refuses to
/// start without a credential. Every case here breaks one of those and expects the gate to say so.
export const OBJECTS_GATE: Gate = {
  id: "objects",
  stage: [
    "api/tests/object_readers_e2e.sh",
    "core/include/cms/object_store.hpp",
    "core/src/object_store.cpp",
    "core/src/object_get.cpp",
    "core/CMakeLists.txt",
    "deploy/host.env.example",
    "deploy/.env.example",
    "deploy/docker-compose.yml",
    "tools/check-objects.sh",
  ],
  // The staged tree is a copy without build products, so the reader comes from the real checkout: the
  // cases are about the gate's assertions, not about compiling the client again.
  command: {
    command: "bash",
    args: ["tools/check-objects.sh"],
    env: {
      CMS_OBJECT_GET: "{repo}/build/safety/core/cms_object_get",
      CMS_API_BINARY: "{repo}/build/safety/api/cms_api",
      CMS_PROBE_REPO: "{repo}",
    },
  },
  cases: [
    {
      name: "control",
      expect: { status: "zero" },
    },
    {
      // A second client: one more file that names the SDK's types is a second place the refusal rules can
      // drift, and the gate's job is to keep the count at one.
      name: "second-client",
      plant: [
        {
          kind: "write",
          path: "api/src/object_store_copy.cpp",
          content: "#include <aws/s3/S3Client.h>\n// a second client, deliberately\n",
        },
      ],
      expect: { status: "non-zero", message: "a second client speaks to the store" },
    },
    {
      // The deployment loses its refusal: with the compose no longer requiring the credential, a stack
      // would start against an allow-all store and nothing would say so.
      name: "credential-not-required",
      plant: [
        {
          kind: "run",
          script:
            "sed -i 's|${CMSLEARN_S3_ACCESS_KEY_ID:?set CMSLEARN_S3_ACCESS_KEY_ID in .env}|${CMSLEARN_S3_ACCESS_KEY_ID:-}|' deploy/docker-compose.yml",
          verify: "grep -q 'CMSLEARN_S3_ACCESS_KEY_ID:-' deploy/docker-compose.yml",
        },
      ],
      expect: { status: "non-zero", message: "does not require CMSLEARN_S3_ACCESS_KEY_ID" },
    },
    {
      // The proof lost: with the readers case forced to skip, the gate must refuse rather than report success.
      // This is the case that would have caught the hole a skipped dependency left in the suite.
      name: "readers-proof-skipped",
      plant: [
        {
          kind: "write",
          path: "api/tests/object_readers_e2e.sh",
          content: "#!/usr/bin/env bash\necho \"  skipped: forced by the falsifier\"\nexit 77\n",
        },
      ],
      expect: { status: "non-zero", message: "the object readers proof did not run" },
    },
    {
      // A credential in a tracked file: the example is read by every operator and committed forever.
      name: "credential-in-example",
      plant: [
        {
          kind: "patch",
          path: "deploy/host.env.example",
          find: "CMSLEARN_S3_SECRET_ACCESS_KEY=\n",
          replace: "CMSLEARN_S3_SECRET_ACCESS_KEY=probe-secret-key\n",
        },
      ],
      expect: { status: "non-zero", message: "ships a credential" },
    },
  ],
};
