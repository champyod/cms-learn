#pragma once

#include <cstdint>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/candidate.hpp>

namespace drogon::orm {
class DbClient;
}

namespace cms::generate {

/// @brief Where steps 1 and 2 write their record: `generation_candidates`, through the worker's own role.
///
/// The interface exists so a test asserts what a step recorded without a database, and the worker writes the
/// same rows through the one implementation that speaks SQL. Neither writes `tasks`: the grant withholds it,
/// so a worker cannot publish or verify whatever it produces (ADR 0015, `deploy/schema/generation.sql`).
class CandidateSink {
 public:
  virtual ~CandidateSink() = default;

  /// @param job_id The generation job the rows belong to.
  /// @param records One row per candidate, in position order; positions are what the judge named.
  /// @return How many rows were written, or the reason the database refused.
  [[nodiscard]] virtual auto record(std::int64_t job_id, const std::vector<CandidateRecord>& records)
      -> Result<std::size_t> = 0;
};

/// @brief A sink that keeps the rows in memory, so a test asserts what a step would have recorded.
class MemoryCandidateSink final : public CandidateSink {
 public:
  [[nodiscard]] auto record(std::int64_t job_id, const std::vector<CandidateRecord>& records)
      -> Result<std::size_t> override;

  /// @return The job the last decision was recorded against.
  [[nodiscard]] auto job_id() const -> std::int64_t { return job_id_; }
  /// @return The rows the last decision wrote, in position order.
  [[nodiscard]] auto rows() const -> const std::vector<CandidateRecord>& { return rows_; }

 private:
  std::int64_t job_id_{0};
  std::vector<CandidateRecord> rows_;
};

/// @brief The sink the worker uses: one transaction per decision, so a refused row leaves none behind.
class SqlCandidateSink final : public CandidateSink {
 public:
  /// @param client The pool the worker connects through; the role behind it is the one the grant names.
  explicit SqlCandidateSink(::drogon::orm::DbClient& client);

  [[nodiscard]] auto record(std::int64_t job_id, const std::vector<CandidateRecord>& records)
      -> Result<std::size_t> override;

 private:
  ::drogon::orm::DbClient& client_;
};

}  // namespace cms::generate
