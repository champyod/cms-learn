#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <cms/error.hpp>

namespace drogon::orm {
class DbClient;
}

namespace cms::judge {

/// @brief What a promotion produced.
struct Promotion {
  /// The digest of the task's own files, which is also the artifact's name.
  std::string digest;
  /// Where the artifact was written.
  std::filesystem::path artifact;
};

/// @brief The certificate row the certifier wrote for one task version.
struct Certificate {
  /// The row's outcome: `verified`, `failed`, or `pending`.
  std::string outcome;
  /// The commit the certifier checked, recorded so a refusal can name the run it read.
  std::string commit_id;
};

/// @brief Where a publish reads its machine evidence: the certificate row, never a claim in the task file.
///
/// The interface exists so the publisher's decision can be proven without a database and so the one SQL read
/// lives in a target that links the connector, while `promote.cpp` stays database-free.
class CertificateSource {
 public:
  virtual ~CertificateSource() = default;

  /// @param task_id The task the certificate must name.
  /// @param task_version The version it must name.
  /// @return The newest certificate for that task version, or nothing when the certifier wrote none.
  [[nodiscard]] virtual auto latest(std::string_view task_id, std::int64_t task_version) const
      -> Result<std::optional<Certificate>> = 0;
};

/// @brief The source the operator's CLI uses: the newest `generation_certificates` row for the task version.
///
/// The role behind @p client needs `SELECT` on `generation_certificates`; the certifier role that writes the
/// row holds it, and any other role the deployer points the publisher at needs it granted. The publisher never
/// writes a certificate, a verification or a task.
class SqlCertificateSource final : public CertificateSource {
 public:
  explicit SqlCertificateSource(std::shared_ptr<::drogon::orm::DbClient> client);

  [[nodiscard]] auto latest(std::string_view task_id, std::int64_t task_version) const
      -> Result<std::optional<Certificate>> override;

 private:
  std::shared_ptr<::drogon::orm::DbClient> client_;
};

/// @brief Publishes a draft, but only on the certificate row the certifier wrote for its version.
///
/// @param task_dir The draft task directory.
/// @param artifact_dir Where the content-addressed artifact is written.
/// @param reference The reference executable the cases are checked against.
/// @param certificates The certificate source the evidence is read from.
/// @return The digest and the artifact path, or the reason publication was refused.
///
/// Three refusals, in this order: an already published task; a certificate row that is missing or does not say
/// `verified`, refused with both the file's claim and the row's outcome named; and a reference that does not
/// reproduce its expected outputs. The order matters — the cheapest refusal is the one that says the machine
/// evidence is missing, not the one that says a byte differed.
///
/// The task file's `verification.status` is never what decides: it is read only so a refusal can name the source
/// that disagreed. The artifact is named by the digest of the published bytes, so the same task published twice
/// yields the same path and two different tasks can never collide.
[[nodiscard]] Result<Promotion> promote_task(const std::filesystem::path& task_dir,
                                             const std::filesystem::path& artifact_dir,
                                             const std::filesystem::path& reference,
                                             const CertificateSource& certificates);

}  // namespace cms::judge
