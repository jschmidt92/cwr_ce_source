#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Core/LocalDb/LocalDbStore.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("Local DB store persists isolated database-key records", "[local_db][persistence]")
{
    const fs::path root = fs::temp_directory_path() /
                          ("poseidon_local_db_store_test_" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(root, ec);

    Poseidon::LocalDb::Store store(root.string());
    REQUIRE(store.Save("actor", "76561198000000000", R"({"name":"Player One","cash":100})"));
    REQUIRE(store.Save("bank", "76561198000000000", R"({"balance":250})"));
    REQUIRE(store.Save("actor", "76561198000000001", R"({"name":"Player Two","cash":50})"));

    std::string actor;
    REQUIRE(store.Load("actor", "76561198000000000", actor));
    REQUIRE(actor == R"({"name":"Player One","cash":100})");
    REQUIRE(fs::is_regular_file(root / "LocalDb" / "schema.json"));
    REQUIRE(store.Exists("actor", "76561198000000000"));
    REQUIRE_FALSE(store.Exists("actor", "missing"));

    const std::vector<std::string> actorKeys = store.List("actor");
    REQUIRE(actorKeys.size() == 2);
    REQUIRE(std::find(actorKeys.begin(), actorKeys.end(), "76561198000000000") != actorKeys.end());
    REQUIRE(std::find(actorKeys.begin(), actorKeys.end(), "76561198000000001") != actorKeys.end());

    std::string bank;
    REQUIRE(store.Load("bank", "76561198000000000", bank));
    REQUIRE(bank == R"({"balance":250})");
    REQUIRE(store.Remove("actor", "76561198000000000"));
    REQUIRE_FALSE(store.Load("actor", "76561198000000000", actor));

    fs::remove_all(root, ec);
}

TEST_CASE("Local DB store rejects unsafe path identifiers", "[local_db][persistence]")
{
    REQUIRE(Poseidon::LocalDb::Store::IsSafeIdentifier("actor.hot-v1"));
    REQUIRE_FALSE(Poseidon::LocalDb::Store::IsSafeIdentifier("../actor"));
    REQUIRE_FALSE(Poseidon::LocalDb::Store::IsSafeIdentifier("actor/other"));
    REQUIRE_FALSE(Poseidon::LocalDb::Store::IsSafeIdentifier(""));
}
