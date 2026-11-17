#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/model.hpp>
#include <cms/object_store.hpp>

namespace drogon::orm {
class DbClient;
}

namespace cms::generate {

/// @brief Where an artefact's bytes live: the store when a tool reads them, a row when they are small.
enum class ArtefactHome : std::uint8_t { row, store };

/// @brief One artefact a step produced, before it is stored: the row's facts, its bytes, and the call behind it.
///
/// A store artefact is named by `object_key`, which the caller builds; its digest is the hash of the bytes the
/// client wrote, so the row can only name content that exists and can be checked on the way back. A row
/// artefact keeps its bytes inline and its digest addresses that text (ADR 0027 §2).
struct Artefact {
  std::int16_t step{0};
  std::string kind;
  std::string locale;
  ArtefactHome home{ArtefactHome::row};
  std::string object_key;
  std::string bytes;
  ModelCall call;
};

/// @brief Where a step's artefacts go: their bytes to the store, their record to generation_artefacts.
///
/// The digest is never a parameter: it is what the client returned when it wrote the bytes, or the hash of the
/// inline text. An artefact whose digest was supplied by hand could name bytes that do not exist, and the read
/// that checks it would be the only place the mistake surfaced (ADR 0023 §2).
class ArtefactSink {
 public:
  virtual ~ArtefactSink() = default;

  /// @param job_id The generation job the rows belong to.
  /// @param artefacts One row per artefact, each already carrying its id, kind, step and bytes.
  /// @return How many rows were written, or the reason the write stopped.
  [[nodiscard]] virtual auto record(std::int64_t job_id, const std::vector<Artefact>& artefacts)
      -> Result<std::size_t> = 0;
};

/// @brief A sink that keeps the rows in memory, so a test asserts what a step would have recorded.
class MemoryArtefactSink final : public ArtefactSink {
 public:
  [[nodiscard]] auto record(std::int64_t job_id, const std::vector<Artefact>& artefacts)
      -> Result<std::size_t> override;

  /// @return The job the last set of artefacts was recorded against.
  [[nodiscard]] auto job_id() const -> std::int64_t { return job_id_; }
  /// @return The digests the last set was recorded with, in order.
  [[nodiscard]] auto digests() const -> const std::vector<std::string>& { return digests_; }

 private:
  std::int64_t job_id_{0};
  std::vector<std::string> digests_;
};

/// @brief The sink the worker uses: the bytes are written first, then every row in one transaction.
class SqlArtefactSink final : public ArtefactSink {
 public:
  /// @param client The pool the worker connects through; the role behind it is the one the grant names.
  /// @param store The client the bytes are written through.
  SqlArtefactSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store);

  [[nodiscard]] auto record(std::int64_t job_id, const std::vector<Artefact>& artefacts)
      -> Result<std::size_t> override;

 private:
  ::drogon::orm::DbClient& client_;
  cms::store::ObjectStore& store_;
};

/// @brief One artefact read back: bytes that matched the digest its row records.
struct StoredArtefact {
  std::string digest;
  std::string bytes;
};

/// @brief Reads the artefact of @p step, @p kind and @p locale for @p job, checking it against the digest the row
/// records.
///
/// A store artefact goes through the verifying read, so bytes that no longer match their address are refused.
/// An inline artefact is hashed here, because the row is its only home.
///
/// @param locale The locale whose row is wanted. Empty asks for the newest row of that step and kind, whatever
/// locale it carries, which is what a step with one artefact per call means.
/// @return The verified payload, `not_found` when the row or object is missing — naming the locale when one was
/// asked for — `conflict` when the bytes do not match, or `unavailable` with the store's reason.
[[nodiscard]] auto read_artefact(::drogon::orm::DbClient& client, const cms::store::ObjectStore& store,
                                 std::int64_t job_id, std::int16_t step, std::string_view kind,
                                 std::string_view locale = {}) -> Result<StoredArtefact>;

}  // namespace cms::generate
