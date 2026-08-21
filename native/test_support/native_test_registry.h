#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace native_test {

struct TestContext {
    std::filesystem::path artifact_directory;
    std::string runner_name;
    std::string suite_name;
    std::string case_name;

    std::filesystem::path artifact_path(
        const std::filesystem::path& filename) const {
        return artifact_directory / filename;
    }
};

class InvalidFixtureError final : public std::runtime_error {
public:
    explicit InvalidFixtureError(const std::string& message)
        : std::runtime_error(message) {}
};

[[noreturn]] inline void invalid_fixture(const std::string& message) {
    throw InvalidFixtureError(message);
}

using TestBody = std::function<void(const TestContext&)>;
using IncidentEntry = int (*)(int, char**);

struct TestCase {
    std::string suite;
    std::string name;
    TestBody body;
};

class Registry {
public:
    void add_case(
        std::string suite,
        std::string name,
        std::function<void()> body) {
        add_context_case(
            std::move(suite),
            std::move(name),
            [body = std::move(body)](const TestContext&) { body(); });
    }

    void add_context_case(
        std::string suite,
        std::string name,
        TestBody body) {
        if (suite.empty() || name.empty() || !body) {
            throw std::invalid_argument(
                "native test registration requires suite, case, and body");
        }
        const auto duplicate = std::find_if(
            cases_.begin(),
            cases_.end(),
            [&](const TestCase& existing) {
                return existing.suite == suite && existing.name == name;
            });
        if (duplicate != cases_.end()) {
            throw std::invalid_argument(
                "duplicate native test case: " + suite + "." + name);
        }
        cases_.push_back({std::move(suite), std::move(name), std::move(body)});
    }

    void add_incident_entry(
        std::string suite,
        std::string name,
        std::filesystem::path artifact_filename,
        IncidentEntry entry) {
        if (entry == nullptr || artifact_filename.empty()) {
            throw std::invalid_argument(
                "incident registration requires an entry and artifact name");
        }
        add_context_case(
            std::move(suite),
            std::move(name),
            [artifact_filename = std::move(artifact_filename), entry](
                const TestContext& context) {
                std::filesystem::create_directories(
                    context.artifact_directory);
                std::string program = context.case_name;
                std::string output_option = "--output";
                std::string output_path =
                    context.artifact_path(artifact_filename).string();
                char* arguments[] = {
                    program.data(), output_option.data(), output_path.data()};
                const int status = entry(3, arguments);
                if (status == 3) {
                    invalid_fixture(
                        "incident trigger or counterfactual was invalid; see " +
                        output_path);
                }
                if (status != 0) {
                    throw std::runtime_error(
                        "incident oracle failed; see " + output_path);
                }
            });
    }

    const std::vector<TestCase>& cases() const noexcept { return cases_; }

private:
    std::vector<TestCase> cases_;
};

struct RunOptions {
    std::optional<std::string> suite;
    std::optional<std::string> case_name;
    std::filesystem::path artifact_directory = "native-test-artifacts";
    bool list_only = false;
};

inline RunOptions parse_options(int argc, char** argv) {
    RunOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        const auto read_value = [&](const char* option) -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    std::string(option) + " requires a value");
            }
            return argv[++index];
        };
        if (argument == "--suite") {
            options.suite = read_value("--suite");
        } else if (argument == "--case") {
            options.case_name = read_value("--case");
        } else if (argument == "--artifacts") {
            options.artifact_directory = read_value("--artifacts");
        } else if (argument == "--list") {
            options.list_only = true;
        } else if (argument == "--help") {
            std::cout
                << "options: [--suite <exact>] [--case <exact or suite.case>] "
                   "[--artifacts <directory>] [--list]\n";
            options.list_only = true;
        } else {
            throw std::invalid_argument(
                "unknown native test option: " + std::string(argument));
        }
    }
    return options;
}

inline bool selected(const TestCase& test, const RunOptions& options) {
    if (options.suite.has_value() && test.suite != *options.suite) {
        return false;
    }
    if (!options.case_name.has_value()) return true;
    return test.name == *options.case_name ||
        test.suite + "." + test.name == *options.case_name;
}

inline int run(
    const Registry& registry,
    int argc,
    char** argv,
    std::string runner_name) {
    try {
        const RunOptions options = parse_options(argc, argv);
        std::vector<const TestCase*> selected_cases;
        for (const auto& test : registry.cases()) {
            if (selected(test, options)) selected_cases.push_back(&test);
        }

        if (options.list_only) {
            for (const auto* test : selected_cases) {
                std::cout << test->suite << "." << test->name << '\n';
            }
            return 0;
        }
        if (selected_cases.empty()) {
            std::cerr << "[" << runner_name
                      << "] no cases matched the requested filter\n";
            return 2;
        }
        std::filesystem::create_directories(options.artifact_directory);

        int passed = 0;
        int failed = 0;
        int invalid = 0;
        const auto runner_started = std::chrono::steady_clock::now();
        for (const auto* test : selected_cases) {
            const auto started = std::chrono::steady_clock::now();
            const TestContext context{
                options.artifact_directory,
                runner_name,
                test->suite,
                test->name,
            };
            try {
                test->body(context);
                ++passed;
                const auto elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started);
                std::cout << "[PASS] " << test->suite << "." << test->name
                          << " (" << std::fixed << std::setprecision(2)
                          << elapsed.count() << " ms)\n";
            } catch (const InvalidFixtureError& error) {
                ++invalid;
                std::cerr << "[INVALID] " << test->suite << "." << test->name
                          << ": " << error.what() << '\n';
            } catch (const std::exception& error) {
                ++failed;
                std::cerr << "[FAIL] " << test->suite << "." << test->name
                          << ": " << error.what() << '\n';
            } catch (...) {
                ++failed;
                std::cerr << "[FAIL] " << test->suite << "." << test->name
                          << ": unknown exception\n";
            }
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - runner_started);
        std::cout << "[" << runner_name << "] selected="
                  << selected_cases.size() << " passed=" << passed
                  << " failed=" << failed << " invalid=" << invalid
                  << " elapsed_ms=" << std::fixed << std::setprecision(2)
                  << elapsed.count() << '\n';
        if (invalid != 0) return 3;
        return failed == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "[" << runner_name << "] " << error.what() << '\n';
        return 2;
    }
}

}  // namespace native_test
