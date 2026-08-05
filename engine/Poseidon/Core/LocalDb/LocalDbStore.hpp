#pragma once

#include <vector>
#include <string>

namespace Poseidon::LocalDb
{
/// File-backed local JSON persistence under the active profile directory.
///
/// Records are intentionally scoped by database and key rather than by a mission save.
/// A dedicated server supplies its __SERVER__ profile directory as root; a local game
/// can supply the active player's profile directory. Neither identifier is ever treated
/// as a path component until it has passed IsSafeIdentifier().
class Store
{
  public:
    static constexpr int SchemaVersion = 1;

    /// Creates <profileDirectory>/LocalDb and writes schema.json when it is absent.
    /// Existing schema versions are preserved so a future migration can inspect them.
    explicit Store(std::string profileDirectory);

    /// Store an opaque JSON document for one database/key record. The write is atomic
    /// on supported platforms: a crash cannot leave a partially-written destination.
    bool Save(const std::string& database, const std::string& key, const std::string& json) const;

    /// Load the exact JSON document stored by Save. Returns false for a missing or
    /// unreadable record; callers can then construct their normal default state.
    bool Load(const std::string& database, const std::string& key, std::string& json) const;

    /// Remove a record. Missing records count as already deleted.
    bool Remove(const std::string& database, const std::string& key) const;

    /// Check whether a database/key record exists.
    bool Exists(const std::string& database, const std::string& key) const;

    /// List record keys stored in one database.
    std::vector<std::string> List(const std::string& database) const;

    /// Valid identifiers are ASCII letters, digits, '-', '_' and '.'. This keeps all
    /// local DB data rooted under its profile directory, including on Windows.
    static bool IsSafeIdentifier(const std::string& value);

    /// The on-disk root: <profileDirectory>/LocalDb.
    const std::string& Root() const { return m_root; }

  private:
    std::string RecordPath(const std::string& database, const std::string& key) const;
    bool EnsureInitialized() const;

    std::string m_root;
};
} // namespace Poseidon::LocalDb
