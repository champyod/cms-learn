import type { Gate } from "../contract.ts";

/// The defect this gate exists for: the sandbox image sets up and its execve fails, so the run says RE —
/// "the probe did not run" — and a gate that accepts RE calls that containment. The fake isolate keeps the
/// sandbox's shape (init gives a box, run reports RE, cleanup succeeds) so the only difference from a
/// working sandbox is that the program is never executed.
const FAKE_ISOLATE = `#!/bin/sh
run=0
init=0
cleanup=0
meta=""
while [ $# -gt 0 ]; do
  case "$1" in
    --init) init=1 ;;
    --cleanup) cleanup=1 ;;
    --run) run=1 ;;
    --meta) shift; meta="$1" ;;
  esac
  shift
done
if [ "$init" = 1 ]; then
  mkdir -p /var/lib/isolate/0/box
  echo /var/lib/isolate/0
  exit 0
fi
if [ "$cleanup" = 1 ]; then
  rm -rf /var/lib/isolate/0
  exit 0
fi
if [ "$run" = 1 ]; then
  [ -n "$meta" ] && printf 'status:RE\\nmessage:Exited with error status 127\\n' > "$meta"
  echo 'Error: execve("./bomb"): No such file or directory' >&2
  exit 1
fi
exit 0
`;

const FAKE_DOCKERFILE = `FROM debian:trixie-slim
COPY fake-isolate /usr/bin/isolate
RUN chmod 0555 /usr/bin/isolate
ENTRYPOINT ["/usr/bin/isolate"]
`;

export const ISOLATION_GATE: Gate = {
  id: "isolation",
  stage: ["judge/isolation"],
  command: {
    command: "bash",
    args: ["{repo}/tools/check-isolation.sh"],
    env: { ISOLATION_DIR: "{root}/judge/isolation" },
  },
  cases: [
    {
      name: "sandbox-sets-up-but-execve-fails",
      plant: [
        { kind: "write", path: "judge/isolation/fake-isolate", content: FAKE_ISOLATE },
        { kind: "write", path: "judge/isolation/Dockerfile", content: FAKE_DOCKERFILE },
      ],
      expect: { status: "non-zero", message: "did not execute" },
    },
  ],
};
