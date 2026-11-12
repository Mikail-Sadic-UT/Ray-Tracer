/// @file test_results_fixtures.cpp
/// @brief Checks the committed results fixtures in tests/data/bench with rt::validate_results.
///
/// scripts/bench/results_schema.py mirrors this validator, and tests/test_run_tapp.py runs the same
/// fixtures through the mirror. Both read tests/data/bench/manifest.json, so a rule that changes in
/// one validator but not the other fails a test.
#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "bench/results.hpp"
#include "core/error.hpp"

namespace {

using json = nlohmann::json;

json read_json(const std::string& name) {
    const std::string path = std::string(RT_TEST_BENCH_DATA_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.good()) << "cannot open " << path;
    return json::parse(in);
}

TEST(ResultsFixtures, ValidFilesPass) {
    const json manifest = read_json("manifest.json");
    ASSERT_FALSE(manifest.at("valid").empty());
    for (const std::string name : manifest.at("valid")) {
        SCOPED_TRACE(name);
        EXPECT_NO_THROW(rt::validate_results(read_json(name)));
    }
}

TEST(ResultsFixtures, InvalidFilesFailNamingTheField) {
    const json manifest = read_json("manifest.json");
    ASSERT_FALSE(manifest.at("invalid").empty());
    for (const json& entry : manifest.at("invalid")) {
        const std::string name = entry.at("file");
        const std::string expected = entry.at("error");
        SCOPED_TRACE(name);
        try {
            rt::validate_results(read_json(name));
            ADD_FAILURE() << "accepted";
        } catch (const rt::Error& error) {
            EXPECT_NE(std::string(error.what()).find(expected), std::string::npos)
                << "message was: " << error.what();
        }
    }
}

}  // namespace
