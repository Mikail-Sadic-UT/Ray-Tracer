/// @file main.cpp
/// @brief Core PFM/AOV quality gate; prints all checks and exits nonzero on any failure.
#include <CLI/CLI.hpp>
#include <exception>
#include <iostream>

#include "image/compare.hpp"

int main(int argc, char** argv) {
    try {
        CLI::App app{"Compare linear PFM images and their primid/lightmask AOV sidecars"};
        std::string reference, test;
        rt::CompareSettings settings;
        double budget_percent = 0.05;
        app.add_option("reference", reference, "Reference PFM")->required();
        app.add_option("test", test, "Test PFM")->required();
        app.add_option("--tau-abs", settings.absolute, "Absolute linear RGB tolerance");
        app.add_option("--tau-rel", settings.relative, "Relative linear RGB tolerance");
        app.add_option("--budget-percent", budget_percent, "Allowed explained mismatch percentage");
        CLI11_PARSE(app, argc, argv);
        settings.mismatch_fraction = budget_percent / 100.0;
        const auto result = rt::compare_images(rt::read_comparison_image(reference),
                                               rt::read_comparison_image(test), settings);
        const auto report = [](const char* name, bool pass, uint64_t count) {
            std::cout << (pass ? "PASS " : "FAIL ") << name << ": " << count << " pixels\n";
        };
        report("nonfinite", result.nonfinite == 0, result.nonfinite);
        report("explained hit mismatch", result.unexplained_hits == 0, result.unexplained_hits);
        report("explained shadow mismatch", result.unexplained_shadows == 0,
               result.unexplained_shadows);
        report("mismatch budget", result.budget_pass, result.explained_mismatches);
        report("shading agreement", result.shading_mismatches == 0, result.shading_mismatches);
        std::cout << "sRGB within 2/255: "
                  << 100.0 * static_cast<double>(result.srgb_close) /
                         static_cast<double>(result.pixels)
                  << "%\n";
        return result.passed() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
