#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string normalize(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            out.push_back('\n');
        } else {
            out.push_back(text[i]);
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    out.push_back('\n');
    return out;
}

std::string quote(const fs::path& path) {
    return "\"" + path.string() + "\"";
}

bool runCommand(const std::string& command, bool expectSuccess = true) {
    int rc = std::system(command.c_str());
    bool success = rc == 0;
    if (success != expectSuccess) {
        std::cerr << "command " << (expectSuccess ? "failed" : "succeeded unexpectedly") << ": " << command << "\n";
        return false;
    }
    return true;
}

bool runGolden(const fs::path& exe, const fs::path& fixtures, const fs::path& outDir, const std::string& name, const std::string& extraArgs, const fs::path& expected) {
    std::cerr << "running fixture " << name;
    if (!extraArgs.empty()) {
        std::cerr << " " << extraArgs;
    }
    std::cerr << "\n";
    fs::path out = outDir / (name + ".out.j");
    std::string command = exe.string() + " " + quote(fixtures / (name + ".in.j")) + " -o " + quote(out) + " " + extraArgs;
    if (!runCommand(command)) {
        return false;
    }
    std::string actual = normalize(readFile(out));
    std::string want = normalize(readFile(expected));
    if (actual != want) {
        std::cerr << "golden mismatch for " << name << "\n";
        std::cerr << "--- actual ---\n" << actual << "--- expected ---\n" << want;
        return false;
    }
    return true;
}

bool runExpectFail(const fs::path& exe, const fs::path& fixtures, const fs::path& outDir, const std::string& name, const std::string& extraArgs) {
    std::cerr << "running negative fixture " << name << "\n";
    fs::path out = outDir / (name + ".out.j");
    std::string command = exe.string() + " " + quote(fixtures / (name + ".in.j")) + " -o " + quote(out) + " " + extraArgs;
    return runCommand(command, false);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: golden_runner <vjassc> <fixtures> <out-dir>\n";
        return 2;
    }
    fs::path exe = argv[1];
    fs::path fixtures = argv[2];
    fs::path outDir = argv[3];
    fs::create_directories(outDir);

    struct GoldenCase {
        std::string name;
        std::string args;
        fs::path expected;
    };

    std::vector<GoldenCase> cases = {
        {"01_globals_native", "", fixtures / "01_globals_native.expected.j"},
        {"02_debug_release", "--release", fixtures / "02_debug_release.expected.release.j"},
        {"02_debug_release", "--debug", fixtures / "02_debug_release.expected.debug.j"},
        {"03_novjass", "", fixtures / "03_novjass.expected.j"},
        {"04_import_root", "", fixtures / "04_import_root.expected.j"},
        {"05_textmacro", "", fixtures / "05_textmacro.expected.j"},
        {"06_library_sort", "", fixtures / "06_library_sort.expected.j"},
        {"07_zinc_basic", "", fixtures / "07_zinc_basic.expected.j"},
        {"08_private_public", "", fixtures / "08_private_public.expected.j"},
        {"09_unsupported_struct", "", fixtures / "09_unsupported_struct.expected.j"},
        {"phase2_struct_basic_vjass", "", fixtures / "phase2_struct_basic_vjass.expected.j"},
        {"phase2_method_vjass", "", fixtures / "phase2_method_vjass.expected.j"},
        {"phase2_static_vjass", "", fixtures / "phase2_static_vjass.expected.j"},
        {"phase2_custom_create_vjass", "", fixtures / "phase2_custom_create_vjass.expected.j"},
        {"phase2_destroy_on_destroy_vjass", "", fixtures / "phase2_destroy_on_destroy_vjass.expected.j"},
        {"phase2_on_init_vjass", "", fixtures / "phase2_on_init_vjass.expected.j"},
        {"phase2_library_struct_vjass", "", fixtures / "phase2_library_struct_vjass.expected.j"},
        {"phase2_zinc_basic", "", fixtures / "phase2_zinc_basic.expected.j"},
        {"phase2_zinc_array", "", fixtures / "phase2_zinc_array.expected.j"},
        {"phase2_fixed_array_zinc", "", fixtures / "phase2_fixed_array_zinc.expected.j"},
        {"phase2_static_code_ref", "", fixtures / "phase2_static_code_ref.expected.j"},
        {"phase2_real_fragment_zinc", "", fixtures / "phase2_real_fragment_zinc.expected.j"},
        {"phase3_static_if_vjass", "", fixtures / "phase3_static_if_vjass.expected.j"},
        {"phase3_static_if_debug", "--debug", fixtures / "phase3_static_if_debug.expected.j"},
        {"phase3_static_if_zinc", "", fixtures / "phase3_static_if_zinc.expected.j"},
        {"phase3_module_vjass_simple", "", fixtures / "phase3_module_vjass_simple.expected.j"},
        {"phase3_module_vjass_nested", "", fixtures / "phase3_module_vjass_nested.expected.j"},
        {"phase3_module_zinc_cross_library", "", fixtures / "phase3_module_zinc_cross_library.expected.j"},
        {"phase3_module_zinc_optional_missing", "", fixtures / "phase3_module_zinc_optional_missing.expected.j"},
        {"phase3_module_oninit_ondestroy", "", fixtures / "phase3_module_oninit_ondestroy.expected.j"},
    };

    bool ok = true;
    for (const auto& testCase : cases) {
        ok = runGolden(exe, fixtures, outDir, testCase.name, testCase.args, testCase.expected) && ok;
    }
    ok = runExpectFail(exe, fixtures, outDir, "phase2_negative_duplicate_field", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase2_negative_method_outside_struct", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase3_negative_static_if_bad_expr", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase3_negative_static_if_missing_endif", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase3_negative_module_missing", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase3_negative_module_cycle", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase3_negative_module_duplicate_field", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase3_negative_module_private_cross_library", "") && ok;

    fs::path stats = outDir / "09.stats.json";
    std::string scan = exe.string() + " " + quote(fixtures / "09_unsupported_struct.in.j") +
                       " --scan-only --allow-unsupported --emit-stats " + quote(stats);
    ok = runCommand(scan) && ok;
    std::string statsText = readFile(stats);
    if (statsText.find("\"structsUnsupported\": 0") == std::string::npos ||
        statsText.find("\"methodsUnsupported\": 0") == std::string::npos) {
        std::cerr << "09_unsupported_struct did not report struct/method unsupported counters at 0\n";
        ok = false;
    }

    stats = outDir / "phase3.stats.json";
    scan = exe.string() + " " + quote(fixtures / "phase3_static_if_zinc.in.j") +
           " --scan-only --allow-unsupported --emit-stats " + quote(stats);
    ok = runCommand(scan) && ok;
    statsText = readFile(stats);
    if (statsText.find("\"staticIfUnsupported\": 0") == std::string::npos ||
        statsText.find("\"modulesUnsupported\": 0") == std::string::npos ||
        statsText.find("\"staticIfs\": 2") == std::string::npos) {
        std::cerr << "phase3 static-if scan did not report expected counters\n";
        ok = false;
    }

    return ok ? 0 : 1;
}
