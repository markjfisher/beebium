// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

// Integration tests for preset management subcommands
//
// These tests run the actual executables as subprocesses to verify
// end-to-end behavior of the preset management workflow.

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Helper to run a command and capture stdout/stderr
struct ProcessResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

ProcessResult run_command(const std::string& command,
                          const std::vector<std::pair<std::string, std::string>>& env_vars = {}) {
    ProcessResult result;
    result.exit_code = -1;

    // Create temporary files for output capture
    auto stdout_filepath = std::filesystem::temp_directory_path() / "beebium_test_stdout.txt";
    auto stderr_filepath = std::filesystem::temp_directory_path() / "beebium_test_stderr.txt";

    // Build command with environment variables
    std::string full_command;
#ifdef _WIN32
    // Windows: use "set VAR=value&& command"
    // IMPORTANT: No space before && - otherwise the space becomes part of the value!
    // Quote the assignment to handle paths with spaces
    for (const auto& [name, value] : env_vars) {
        full_command += "set \"" + name + "=" + value + "\"&& ";
    }
    full_command += command;
    full_command += " >\"" + stdout_filepath.string() + "\" 2>\"" + stderr_filepath.string() + "\"";
#else
    // Unix: use "VAR=value command"
    for (const auto& [name, value] : env_vars) {
        full_command += name + "=\"" + value + "\" ";
    }
    full_command += command;
    full_command += " >" + stdout_filepath.string() + " 2>" + stderr_filepath.string();
#endif

    result.exit_code = std::system(full_command.c_str());
    // WEXITSTATUS macro to get actual exit code on Unix
#ifdef _WIN32
    // On Windows, system() returns the exit code directly
#else
    if (WIFEXITED(result.exit_code)) {
        result.exit_code = WEXITSTATUS(result.exit_code);
    }
#endif

    // Read stdout
    if (std::ifstream f(stdout_filepath); f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        result.stdout_output = ss.str();
    }

    // Read stderr
    if (std::ifstream f(stderr_filepath); f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        result.stderr_output = ss.str();
    }

    // Clean up temp files
    std::filesystem::remove(stdout_filepath);
    std::filesystem::remove(stderr_filepath);

    return result;
}

// Get path to executable - looks in common locations
std::filesystem::path find_executable(const std::string& name) {
#ifdef _WIN32
    std::string exe_name = name + ".exe";
#else
    std::string exe_name = name;
#endif

    // Check environment variable first
    if (const char* env = std::getenv("BEEBIUM_SERVERS_DIRPATH")) {
        auto path = std::filesystem::path(env) / exe_name;
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    // Check relative to build directory (typical CMake layout)
    std::vector<std::filesystem::path> search_paths = {
        std::filesystem::current_path() / "src" / "server" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / exe_name,
        std::filesystem::current_path() / "build" / "src" / "server" / exe_name,
#ifdef _WIN32
        // MSVC puts executables in Release/Debug subdirectories
        std::filesystem::current_path() / "src" / "server" / "Release" / exe_name,
        std::filesystem::current_path() / "src" / "server" / "Debug" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / "Release" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / "Debug" / exe_name,
#endif
    };

    for (const auto& path : search_paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return exe_name;  // Fall back to just the name (rely on PATH)
}

// RAII helper for temporary directories
class TempDirectory {
public:
    TempDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("beebium_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

private:
    std::filesystem::path path_;
};

const std::string EXECUTABLE = find_executable("beebium-model-b").string();

}  // namespace

// ============================================================================
// list-presets integration tests
// ============================================================================

TEST_CASE("list-presets: returns OK", "[integration][preset][list-presets]") {
    auto result = run_command(EXECUTABLE + " list-presets");

    REQUIRE(result.exit_code == 0);
}

TEST_CASE("list-presets --json: outputs valid JSON array", "[integration][preset][list-presets]") {
    auto result = run_command(EXECUTABLE + " list-presets --json");

    REQUIRE(result.exit_code == 0);
    // Basic JSON structure check - should start with {"presets": or similar
    REQUIRE_FALSE(result.stdout_output.empty());
    REQUIRE((result.stdout_output.find('[') != std::string::npos ||
             result.stdout_output.find('{') != std::string::npos));
}

TEST_CASE("list-presets --help: returns OK", "[integration][preset][list-presets]") {
    auto result = run_command(EXECUTABLE + " list-presets --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// report-presets-dirpath integration tests
// ============================================================================

TEST_CASE("report-presets-dirpath: returns a path", "[integration][preset][report-presets-dirpath]") {
    auto result = run_command(EXECUTABLE + " report-presets-dirpath");

    REQUIRE(result.exit_code == 0);
    REQUIRE_FALSE(result.stdout_output.empty());
    // Should contain a path separator
    REQUIRE((result.stdout_output.find('/') != std::string::npos ||
             result.stdout_output.find('\\') != std::string::npos));
}

TEST_CASE("report-presets-dirpath: respects BEEBIUM_USER_PRESETS_DIRPATH", "[integration][preset][report-presets-dirpath]") {
    TempDirectory temp_dir;

    auto result = run_command(
        EXECUTABLE + " report-presets-dirpath",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find(temp_dir.path().string()) != std::string::npos);
}

// ============================================================================
// show-preset integration tests
// ============================================================================

TEST_CASE("show-preset: nonexistent preset returns NOINPUT (66)", "[integration][preset][show-preset]") {
    auto result = run_command(EXECUTABLE + " show-preset nonexistent-preset-12345");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("show-preset --help: returns OK", "[integration][preset][show-preset]") {
    auto result = run_command(EXECUTABLE + " show-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// create-preset integration tests
// ============================================================================

TEST_CASE("create-preset: missing --name returns USAGE (64)", "[integration][preset][create-preset]") {
    auto result = run_command(EXECUTABLE + " create-preset");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("create-preset --output: creates preset file", "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "test-preset.preset.beebium";

    auto result = run_command(EXECUTABLE + " create-preset --name \"Test Preset\" --output \"" +
                              output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    // Verify content
    std::ifstream f(output_filepath);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("\"model\": \"model-b\"") != std::string::npos);
    REQUIRE(content.find("\"name\": \"Test Preset\"") != std::string::npos);
}

TEST_CASE("create-preset: creates preset in user directory", "[integration][preset][create-preset]") {
    TempDirectory temp_dir;

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"My Test Setup\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    // Output should be the preset ID
    REQUIRE(result.stdout_output.find("my-test-setup") != std::string::npos);

    // Verify file was created
    auto preset_filepath = temp_dir.path() / "my-test-setup.preset.beebium";
    REQUIRE(std::filesystem::exists(preset_filepath));
}

TEST_CASE("create-preset --help: returns OK", "[integration][preset][create-preset]") {
    auto result = run_command(EXECUTABLE + " create-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// delete-preset integration tests
// ============================================================================

TEST_CASE("delete-preset: missing ID returns USAGE (64)", "[integration][preset][delete-preset]") {
    auto result = run_command(EXECUTABLE + " delete-preset");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("delete-preset: nonexistent preset returns NOINPUT (66)", "[integration][preset][delete-preset]") {
    auto result = run_command(EXECUTABLE + " delete-preset nonexistent-preset-12345");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("delete-preset: deletes user preset", "[integration][preset][delete-preset]") {
    TempDirectory temp_dir;

    // First create a preset
    auto preset_filepath = temp_dir.path() / "deleteme.preset.beebium";
    {
        std::ofstream f(preset_filepath);
        f << R"({"model": "model-b", "name": "Delete Me"})";
    }
    REQUIRE(std::filesystem::exists(preset_filepath));

    // Delete it
    auto result = run_command(
        EXECUTABLE + " delete-preset deleteme",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE_FALSE(std::filesystem::exists(preset_filepath));
}

TEST_CASE("delete-preset --help: returns OK", "[integration][preset][delete-preset]") {
    auto result = run_command(EXECUTABLE + " delete-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// import-preset integration tests
// ============================================================================

TEST_CASE("import-preset: missing filepath returns USAGE (64)", "[integration][preset][import-preset]") {
    auto result = run_command(EXECUTABLE + " import-preset");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("import-preset: nonexistent file returns NOINPUT (66)", "[integration][preset][import-preset]") {
    auto result = run_command(EXECUTABLE + " import-preset /nonexistent/path.preset.beebium");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("import-preset: imports valid preset", "[integration][preset][import-preset]") {
    TempDirectory source_dir;
    TempDirectory dest_dir;

    // Create source preset
    auto source_filepath = source_dir.path() / "importme.preset.beebium";
    {
        std::ofstream f(source_filepath);
        f << R"({"model": "model-b", "name": "Import Me"})";
    }

    auto result = run_command(
        EXECUTABLE + " import-preset \"" + source_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", dest_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("importme") != std::string::npos);

    // Verify file was created in dest directory
    auto imported_filepath = dest_dir.path() / "importme.preset.beebium";
    REQUIRE(std::filesystem::exists(imported_filepath));
}

TEST_CASE("import-preset: rejects wrong model", "[integration][preset][import-preset]") {
    TempDirectory temp_dir;

    // Create preset with wrong model
    auto source_filepath = temp_dir.path() / "wrong-model.preset.beebium";
    {
        std::ofstream f(source_filepath);
        f << R"({"model": "wrong-model", "name": "Wrong Model"})";
    }

    auto result = run_command(EXECUTABLE + " import-preset \"" + source_filepath.string() + "\"");

    REQUIRE(result.exit_code == 65);  // DATAERR
    REQUIRE(result.stderr_output.find("does not match") != std::string::npos);
}

TEST_CASE("import-preset --help: returns OK", "[integration][preset][import-preset]") {
    auto result = run_command(EXECUTABLE + " import-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// export-preset integration tests
// ============================================================================

TEST_CASE("export-preset: missing ID returns USAGE (64)", "[integration][preset][export-preset]") {
    auto result = run_command(EXECUTABLE + " export-preset --output /tmp/out.preset.beebium");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("export-preset: missing --output returns USAGE (64)", "[integration][preset][export-preset]") {
    auto result = run_command(EXECUTABLE + " export-preset some-id");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("export-preset: nonexistent preset returns NOINPUT (66)", "[integration][preset][export-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "exported.preset.beebium";

    auto result = run_command(EXECUTABLE + " export-preset nonexistent-12345 --output \"" +
                              output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("export-preset: exports user preset", "[integration][preset][export-preset]") {
    TempDirectory user_dir;
    TempDirectory output_dir;

    // Create source preset in user directory
    auto source_filepath = user_dir.path() / "mypreset.preset.beebium";
    {
        std::ofstream f(source_filepath);
        f << R"({"model": "model-b", "name": "My Preset"})";
    }

    auto output_filepath = output_dir.path() / "exported.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " export-preset mypreset --output \"" + output_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    // Verify content matches
    std::ifstream f(output_filepath);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("\"name\": \"My Preset\"") != std::string::npos);
}

TEST_CASE("export-preset --help: returns OK", "[integration][preset][export-preset]") {
    auto result = run_command(EXECUTABLE + " export-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// End-to-end workflow tests
// ============================================================================

TEST_CASE("Full preset workflow: create, export, import, delete", "[integration][preset][workflow]") {
    TempDirectory user_dir;
    TempDirectory export_dir;
    TempDirectory import_dir;

    // Step 1: Create a preset
    auto create_result = run_command(
        EXECUTABLE + " create-preset --name \"Workflow Test\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(create_result.exit_code == 0);

    std::string preset_id = create_result.stdout_output;
    // Trim whitespace
    while (!preset_id.empty() && (preset_id.back() == '\n' || preset_id.back() == '\r')) {
        preset_id.pop_back();
    }
    REQUIRE_FALSE(preset_id.empty());

    // Verify preset file exists
    auto preset_filepath = user_dir.path() / (preset_id + ".preset.beebium");
    REQUIRE(std::filesystem::exists(preset_filepath));

    // Step 2: Export the preset
    auto export_filepath = export_dir.path() / "exported.preset.beebium";
    auto export_result = run_command(
        EXECUTABLE + " export-preset " + preset_id + " --output \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(export_result.exit_code == 0);
    REQUIRE(std::filesystem::exists(export_filepath));

    // Step 3: Delete the original
    auto delete_result = run_command(
        EXECUTABLE + " delete-preset " + preset_id,
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(delete_result.exit_code == 0);
    REQUIRE_FALSE(std::filesystem::exists(preset_filepath));

    // Step 4: Import from export
    auto import_result = run_command(
        EXECUTABLE + " import-preset \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", import_dir.path().string()}}
    );
    REQUIRE(import_result.exit_code == 0);

    // Verify imported file exists
    auto imported_filepath = import_dir.path() / "exported.preset.beebium";
    REQUIRE(std::filesystem::exists(imported_filepath));
}
