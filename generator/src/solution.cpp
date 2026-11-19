#include <cms/generate/solution.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cms/generate/data_frame.hpp>
#include <cms/generate/specification.hpp>

namespace cms::generate {
namespace {

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

constexpr std::string_view REFERENCE_ANSWER = "the reference solution";
constexpr std::string_view PARTIAL_ANSWER = "a subtask's solution";
constexpr std::int32_t REFERENCE_POSITION = 0;

}  // namespace

auto reference_prompt(const Specification& specification, std::string_view oracle_source) -> Result<std::string> {
  const auto spec = frame_data(ModelData{.label = "the specification", .text = specification.input + " -> " +
                                                                                   specification.output + "; bounds " +
                                                                                   std::to_string(specification.bounds.min_n) + ".." +
                                                                                   std::to_string(specification.bounds.max_n)});
  const auto oracle = frame_data(ModelData{.label = "the brute-force oracle", .text = std::string{oracle_source}});
  if (!spec || !oracle) {
    return std::unexpected(!spec ? spec.error() : oracle.error());
  }
  return "You are step 5 of a task-generation pipeline. Write the reference solution: a complete C++17 program\n"
         "that reads the input and prints the answer the specification names. It must finish inside the derived\n"
         "limit and agree with the oracle on every case.\n\n" +
         *spec + "\n\n" + *oracle + "\n\nAnswer with the program and nothing else.\n";
}

auto partial_prompt(const Specification& specification, const SubtaskConstraint& subtask,
                    std::string_view oracle_source) -> Result<std::string> {
  const auto oracle = frame_data(ModelData{.label = "the brute-force oracle", .text = std::string{oracle_source}});
  const auto constraint =
      frame_data(ModelData{.label = "the subtask's constraint", .text = cms::bounds_text(subtask.bounds)});
  const auto bounds = frame_data(ModelData{.label = "the task's bounds",
                                           .text = "n from " + std::to_string(specification.bounds.min_n) + " to " +
                                                   std::to_string(specification.bounds.max_n) + ", values " +
                                                   std::to_string(specification.bounds.value_min) + ".." +
                                                   std::to_string(specification.bounds.value_max)});
  if (!oracle || !constraint || !bounds) {
    return std::unexpected(!oracle ? oracle.error() : (!constraint ? constraint.error() : bounds.error()));
  }
  return "You are step 5 of a task-generation pipeline. Write the solution a contestant would submit to earn\n"
         "subtask " + std::to_string(subtask.position) + " and no more: it must pass every case that satisfies the\n"
         "constraint below and must fail at least one case the task admits beyond it. A solution that passes\n"
         "everything is not a partial solution and will be refused.\n\n" +
         *bounds + "\n\n" + *constraint + "\n\n" + *oracle + "\n\nAnswer with the program and nothing else.\n";
}

auto parse_reference_answer(std::string_view answer, const ModelCall& call) -> Result<Step5Answer> {
  if (answer.find("int main") == std::string_view::npos) {
    return std::unexpected(refuse(std::string{REFERENCE_ANSWER} + " is not a complete program: it has no int main"));
  }
  return Step5Answer{.reference = WrittenSolution{.position = REFERENCE_POSITION,
                                                  .source = std::string{answer},
                                                  .call = call},
                     .partials = {},
                     .reference_answer = std::string{answer}};
}

auto parse_partial_answer(std::string_view answer, std::int32_t position, const ModelCall& call,
                          Step5Answer& into) -> Result<void> {
  if (answer.find("int main") == std::string_view::npos) {
    return std::unexpected(refuse("the solution for subtask " + std::to_string(position) +
                                  " is not a complete program: it has no int main"));
  }
  const auto duplicate =
      std::find_if(into.partials.begin(), into.partials.end(),
                   [&](const WrittenSolution& partial) { return partial.position == position; });
  if (duplicate != into.partials.end()) {
    return std::unexpected(refuse("the answer carries two solutions for subtask " + std::to_string(position)));
  }
  into.partials.push_back(
      WrittenSolution{.position = position, .source = std::string{answer}, .call = call});
  return {};
}

auto write_solutions(ModelClient& client, const Specification& specification, std::string_view oracle_source)
    -> Result<Step5Answer> {
  if (specification.subtasks.empty()) {
    return std::unexpected(refuse("the specification names no subtask, so there is no partial solution to write: "
                                  "a task without subtasks has no separate group whose weight a weaker solution "
                                  "would earn"));
  }
  const auto reference_request = reference_prompt(specification, oracle_source);
  if (!reference_request) {
    return std::unexpected(reference_request.error());
  }
  const auto reference = client.complete(*reference_request);
  if (!reference) {
    return std::unexpected(reference.error());
  }
  auto answer = parse_reference_answer(reference->text, reference->call);
  if (!answer) {
    return std::unexpected(answer.error());
  }
  for (const auto& subtask : specification.subtasks) {
    const auto request = partial_prompt(specification, subtask, oracle_source);
    if (!request) {
      return std::unexpected(request.error());
    }
    const auto completion = client.complete(*request);
    if (!completion) {
      return std::unexpected(completion.error());
    }
    const auto added = parse_partial_answer(completion->text, subtask.position, completion->call, *answer);
    if (!added) {
      return std::unexpected(added.error());
    }
  }
  return answer;
}

auto MemorySolutionSink::record(std::int64_t job_id, const Step5Answer& answer,
                                const std::vector<PartialProof>& proofs) -> Result<std::size_t> {
  (void)answer;
  job_id_ = job_id;
  proofs_ = proofs;
  return proofs_.size() + 1;
}

}  // namespace cms::generate
