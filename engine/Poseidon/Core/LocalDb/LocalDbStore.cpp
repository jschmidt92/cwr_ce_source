#include <Poseidon/Core/LocalDb/LocalDbStore.hpp>

#include <cctype>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Poseidon::LocalDb
{
namespace fs = std::filesystem;

namespace
{
#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};

    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return {};

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};

    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return {};

    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}
#endif

fs::path FsPathFromUtf8(const std::string& path)
{
#ifdef _WIN32
    return fs::path(Utf8ToWide(path));
#else
    return fs::path(path);
#endif
}

std::string FsPathToUtf8(const fs::path& path)
{
#ifdef _WIN32
    return WideToUtf8(path.wstring());
#else
    return path.string();
#endif
}

bool WriteTextFile(const fs::path& path, const std::string& contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return output.good();
}

std::vector<char> ReadBinaryFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool ReplaceFileAtomically(const fs::path& source, const fs::path& destination)
{
#ifdef _WIN32
    // MoveFileExW replaces an existing destination as one operation, unlike a
    // remove-then-rename sequence that risks losing a player's record on a crash.
    return MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    std::error_code ec;
    fs::rename(source, destination, ec); // POSIX rename replaces atomically.
    return !ec;
#endif
}

std::string SchemaDocument()
{
    return "{\n  \"format\": \"local-db-store\",\n  \"schemaVersion\": " + std::to_string(Store::SchemaVersion) +
           "\n}\n";
}
} // namespace

Store::Store(std::string profileDirectory)
{
    if (!profileDirectory.empty())
        m_root = FsPathToUtf8(FsPathFromUtf8(profileDirectory) / "LocalDb");
}

bool Store::IsSafeIdentifier(const std::string& value)
{
    if (value.empty() || value == "." || value == "..")
        return false;

    for (unsigned char c : value)
    {
        if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}

std::string Store::RecordPath(const std::string& database, const std::string& key) const
{
    if (m_root.empty() || !IsSafeIdentifier(database) || !IsSafeIdentifier(key))
        return {};
    return FsPathToUtf8(FsPathFromUtf8(m_root) / database / (key + ".json"));
}

bool Store::EnsureInitialized() const
{
    if (m_root.empty())
        return false;

    std::error_code ec;
    const fs::path rootPath = FsPathFromUtf8(m_root);
    fs::create_directories(rootPath, ec);
    if (ec || !fs::is_directory(rootPath, ec))
        return false;

    const fs::path schemaPath = rootPath / "schema.json";
    if (fs::exists(schemaPath, ec))
        return !ec;

    const std::string schema = SchemaDocument();
    return WriteTextFile(schemaPath, schema);
}

bool Store::Save(const std::string& database, const std::string& key, const std::string& json) const
{
    const std::string recordPath = RecordPath(database, key);
    if (recordPath.empty() || json.empty() || !EnsureInitialized())
        return false;

    const fs::path destination = FsPathFromUtf8(recordPath);
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec)
        return false;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temporary = FsPathFromUtf8(FsPathToUtf8(destination) + ".tmp." + std::to_string(nonce));
    if (!WriteTextFile(temporary, json))
        return false;

    if (ReplaceFileAtomically(temporary, destination))
        return true;

    fs::remove(temporary, ec);
    return false;
}

bool Store::Load(const std::string& database, const std::string& key, std::string& json) const
{
    json.clear();
    const std::string recordPath = RecordPath(database, key);
    if (recordPath.empty() || !EnsureInitialized())
        return false;

    const std::vector<char> bytes = ReadBinaryFile(FsPathFromUtf8(recordPath));
    if (bytes.empty())
        return false;
    json.assign(bytes.begin(), bytes.end());
    return true;
}

bool Store::Remove(const std::string& database, const std::string& key) const
{
    const std::string recordPath = RecordPath(database, key);
    if (recordPath.empty() || !EnsureInitialized())
        return false;

    std::error_code ec;
    fs::remove(FsPathFromUtf8(recordPath), ec);
    return !ec;
}

bool Store::Exists(const std::string& database, const std::string& key) const
{
    const std::string recordPath = RecordPath(database, key);
    if (recordPath.empty() || !EnsureInitialized())
        return false;

    std::error_code ec;
    return fs::is_regular_file(FsPathFromUtf8(recordPath), ec) && !ec;
}

std::vector<std::string> Store::List(const std::string& database) const
{
    std::vector<std::string> keys;
    if (m_root.empty() || !IsSafeIdentifier(database) || !EnsureInitialized())
        return keys;

    const fs::path databasePath = FsPathFromUtf8(m_root) / database;
    std::error_code ec;
    if (!fs::is_directory(databasePath, ec) || ec)
        return keys;

    for (const fs::directory_entry& entry : fs::directory_iterator(databasePath, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || ec)
            continue;
        const fs::path path = entry.path();
        if (path.extension() == ".json")
            keys.push_back(FsPathToUtf8(path.stem()));
    }
    return keys;
}
} // namespace Poseidon::LocalDb
