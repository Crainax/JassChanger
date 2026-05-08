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

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: golden_runner <vjassc> <fixtures> <out-dir>\n";
        return 2;
    }
    fs::path exe = argv[1];
    fs::path fixtures = argv[2];
    fs::path outDir = argv[3];
    fs::create_directories(outDir);

    bool ok = true;
    ok = runGolden(exe, fixtures, outDir, "01_globals_native", "", fixtures / "01_globals_native.expected.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "02_debug_release", "--release", fixtures / "02_debug_release.expected.release.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "02_debug_release", "--debug", fixtures / "02_debug_release.expected.debug.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "03_novjass", "", fixtures / "03_novjass.expected.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "04_import_root", "", fixtures / "04_import_root.expected.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "05_textmacro", "", fixtures / "05_textmacro.expected.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "06_library_sort", "", fixtures / "06_library_sort.expected.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "07_zinc_basic", "", fixtures / "07_zinc_basic.expected.j") && ok;
    ok = runGolden(exe, fixtures, outDir, "08_private_public", "", fixtures / "08_private_public.expected.j") && ok;

    fs::path stats = outDir / "09.stats.json";
    std::string scan = exe.string() + " " + quote(fixtures / "09_unsupported_struct.in.j") +
                       " --scan-only --allow-unsupported --emit-stats " + quote(stats);
    ok = runCommand(scan) && ok;
    std::string statsText = readFile(stats);
    if (statsText.find("\"structsUnsupported\": 1") == std::string::npos) {
        std::cerr << "09_unsupported_struct did not report structsUnsupported=1\n";
        ok = false;
    }
    std::string codegen = exe.string() + " " + quote(fixtures / "09_unsupported_struct.in.j") + " -o " + quote(outDir / "09.out.j");
    ok = runCommand(codegen, false) && ok;

    return ok ? 0 : 1;
}
