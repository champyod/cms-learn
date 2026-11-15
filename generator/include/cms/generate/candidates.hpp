#pragma once

#include <cstdint>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/candidate.hpp>
#include <cms/generate/judge.hpp>

namespace drogon::orm {
class DbClient;
}

namespace cms::generate {

/// @brief Where steps 1 and 2 write their record: `generation_candidates` and the calls behind them.
///
/// ADR 0030 makes the call part of the record: every model call leaves one `generation_artefacts` row, and a
/// candidate names the call that produced it, in the same transaction. A candidate without its call is not
/// traceable, so the column is required and this sink is the only writer that supplies it.
///
/// The interface exists so a test asserts what a step recorded without a database, and the worker writes the
/// same rows through the one implementation that speaks SQL. Neither writes `tasks`: the grant withholds it,
/// so a worker cannot publish or verify whatever it produces (ADR 0015, `deploy/schema/generation.sql`).
class CandidateSink {
 public:
  virtual ~CandidateSink() = default;

  /// @param job_id The generation job the rows belong to.
  /// @param judgement The decision: one record per candidate, in position order, plus the call that made it.
  /// @return How many candidate rows were written, or the reason the database refused.
  [[nodiscard]] virtual auto record(std::int64_t job_id, const Judgement& judgement)
      -> Result<std::size_t> = 0;
};

/// @brief A sink that keeps what a step would have written, so a test needs no database.
class MemoryCandidateSink final : public CandidateSink {
 public:
  [[nodiscard]] auto record(std::int64_t job_id, const Judgement& judgement)
      -> Result<std::size_t> override;

  /// @return The job the last decision was recorded against.
  [[nodiscard]] auto job_id() const -> std::int64_t { return job_id_; }
  /// @return The decision the last record wrote.
  [[nodiscard]] auto judgement() const -> const Judgement& { return judgement_; }
  /// @return The calls the last record would have written one artefact row for, in write order.
  [[nodiscard]] auto calls() const -> const std::vector<ModelCall>& { return calls_; }

 private:
  std::int64_t job_id_{0};
  Judgement judgement_;
  std::vector<ModelCall> calls_;
};

/// @brief The sink the worker uses: one transaction per decision, so a refused row leaves none behind.
class SqlCandidateSink final : public CandidateSink {
 public:
  /// @param client The pool the worker connects through; the role behind it is the one the grant names.
  explicit SqlCandidateSink(::drogon::orm::DbClient& client);

  [[nodiscard]] auto record(std::int64_t job_id, const Judgement& judgement)
      -> Result<std::size_t> override;

 private:
  ::drogon::orm::DbClient& client_;
};

}  // namespace cms::generate
