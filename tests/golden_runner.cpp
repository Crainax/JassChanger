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
        {"phase4_function_interface_execute", "", fixtures / "phase4_function_interface_execute.expected.j"},
        {"phase4_function_interface_evaluate", "", fixtures / "phase4_function_interface_evaluate.expected.j"},
        {"phase4_nested_evaluate", "", fixtures / "phase4_nested_evaluate.expected.j"},
        {"phase4_interface_as_function_param", "", fixtures / "phase4_interface_as_function_param.expected.j"},
        {"phase4_bare_function_value", "", fixtures / "phase4_bare_function_value.expected.j"},
        {"phase4_static_method_interface", "", fixtures / "phase4_static_method_interface.expected.j"},
        {"phase4_function_object_evaluate", "", fixtures / "phase4_function_object_evaluate.expected.j"},
        {"phase4_function_name", "", fixtures / "phase4_function_name.expected.j"},
        {"phase4_zinc_lambda_code", "", fixtures / "phase4_zinc_lambda_code.expected.j"},
        {"phase4_zinc_lambda_trigger_action", "", fixtures / "phase4_zinc_lambda_trigger_action.expected.j"},
        {"phase4_zinc_lambda_interface", "", fixtures / "phase4_zinc_lambda_interface.expected.j"},
        {"phase5_multiple_lambdas_same_call", "", fixtures / "phase5_multiple_lambdas_same_call.expected.j"},
        {"phase5_nested_lambda", "", fixtures / "phase5_nested_lambda.expected.j"},
        {"phase5_comment_lambda_ignored", "", fixtures / "phase5_comment_lambda_ignored.expected.j"},
        {"phase6_range_for_zinc", "", fixtures / "phase6_range_for_zinc.expected.j"},
        {"phase6_method_chain_zinc", "", fixtures / "phase6_method_chain_zinc.expected.j"},
        {"phase6_thistype_array_typeid", "", fixtures / "phase6_thistype_array_typeid.expected.j"},
        {"phase7_block_comment_globals", "", fixtures / "phase7_block_comment_globals.expected.j"},
        {"phase7_multidim_global_zinc", "", fixtures / "phase7_multidim_global_zinc.expected.j"},
        {"phase7_lambda_private_multidim", "", fixtures / "phase7_lambda_private_multidim.expected.j"},
        {"phase7_struct_bare_members", "", fixtures / "phase7_struct_bare_members.expected.j"},
        {"phase8_lambda_before_use", "", fixtures / "phase8_lambda_before_use.expected.j"},
        {"phase8_lambda_calls_helper", "", fixtures / "phase8_lambda_calls_helper.expected.j"},
        {"phase8_indexed_struct_field", "", fixtures / "phase8_indexed_struct_field.expected.j"},
        {"phase8_indexed_struct_method", "", fixtures / "phase8_indexed_struct_method.expected.j"},
        {"phase8_nested_indexed_struct_field", "", fixtures / "phase8_nested_indexed_struct_field.expected.j"},
        {"phase8_zinc_inline_if_return", "", fixtures / "phase8_zinc_inline_if_return.expected.j"},
        {"phase8_zinc_inline_if_call", "", fixtures / "phase8_zinc_inline_if_call.expected.j"},
        {"phase8_zinc_inline_if_assignment", "", fixtures / "phase8_zinc_inline_if_assignment.expected.j"},
        {"phase8_comma_local_simple", "", fixtures / "phase8_comma_local_simple.expected.j"},
        {"phase8_comma_local_initializer", "", fixtures / "phase8_comma_local_initializer.expected.j"},
        {"phase8_comma_local_array", "", fixtures / "phase8_comma_local_array.expected.j"},
        {"phase9_method_chain_function_receiver", "", fixtures / "phase9_method_chain_function_receiver.expected.j"},
        {"phase9_method_chain_nested_receiver", "", fixtures / "phase9_method_chain_nested_receiver.expected.j"},
        {"phase9_instance_method_interface", "", fixtures / "phase9_instance_method_interface.expected.j"},
        {"phase9_zinc_public_block_globals", "", fixtures / "phase9_zinc_public_block_globals.expected.j"},
        {"phase9_static_this_field", "", fixtures / "phase9_static_this_field.expected.j"},
        {"phase10_zinc_chain_interface_continuation", "", fixtures / "phase10_zinc_chain_interface_continuation.expected.j"},
        {"phase11_undefined_private_global_rewrite", "", fixtures / "phase11_undefined_private_global_rewrite.expected.j"},
        {"phase11_boolean_integer_interface_id", "", fixtures / "phase11_boolean_integer_interface_id.expected.j"},
        {"phase11_code_callback_no_arg_ok", "", fixtures / "phase11_code_callback_no_arg_ok.expected.j"},
        {"phase11_code_callback_param_rejected_or_adapted", "", fixtures / "phase11_code_callback_param_rejected_or_adapted.expected.j"},
        {"phase11_generated_wrapper_return_default", "", fixtures / "phase11_generated_wrapper_return_default.expected.j"},
        {"phase11_source_missing_return_not_patched", "", fixtures / "phase11_source_missing_return_not_patched.expected.j"},
        {"phase11_execute_func_bridge_noarg_cycle", "", fixtures / "phase11_execute_func_bridge_noarg_cycle.expected.j"},
        {"phase11_environment_symbol_report", "", fixtures / "phase11_environment_symbol_report.expected.j"},
        {"phase12_zinc_string_equals_call", "", fixtures / "phase12_zinc_string_equals_call.expected.j"},
        {"phase12_zinc_arithmetic_continuation", "", fixtures / "phase12_zinc_arithmetic_continuation.expected.j"},
        {"phase12_zinc_else_if_tail_return", "", fixtures / "phase12_zinc_else_if_tail_return.expected.j"},
        {"phase12_array_struct_receiver_rewrite", "", fixtures / "phase12_array_struct_receiver_rewrite.expected.j"},
        {"phase12_struct_deallocate", "", fixtures / "phase12_struct_deallocate.expected.j"},
        {"phase12_lambda_default_return", "", fixtures / "phase12_lambda_default_return.expected.j"},
        {"phase12_zinc_method_interface_param", "", fixtures / "phase12_zinc_method_interface_param.expected.j"},
        {"phase13_forward_bridge_noarg", "", fixtures / "phase13_forward_bridge_noarg.expected.j"},
        {"phase13_external_env_symbol", "", fixtures / "phase13_external_env_symbol.expected.j"},
        {"phase13_raw_code_param_lambda_known_context", "", fixtures / "phase13_raw_code_param_lambda_known_context.expected.j"},
        {"phase14_cycle_prefers_noarg_bridge", "", fixtures / "phase14_cycle_prefers_noarg_bridge.expected.j"},
        {"phase14_cycle_method_caller_args", "", fixtures / "phase14_cycle_method_caller_args.expected.j"},
        {"phase14_array_index_parentheses_idempotent", "", fixtures / "phase14_array_index_parentheses_idempotent.expected.j"},
        {"phase14_function_object_evaluate_args", "", fixtures / "phase14_function_object_evaluate_args.expected.j"},
        {"phase14_function_object_evaluate_cycle", "", fixtures / "phase14_function_object_evaluate_cycle.expected.j"},
        {"phase14_function_object_execute_cycle_args", "", fixtures / "phase14_function_object_execute_cycle_args.expected.j"},
        {"phase14_function_object_evaluate_cycle_args", "", fixtures / "phase14_function_object_evaluate_cycle_args.expected.j"},
        {"phase14_function_interface_usage_modes", "", fixtures / "phase14_function_interface_usage_modes.expected.j"},
        {"phase14_library_default_oninit", "", fixtures / "phase14_library_default_oninit.expected.j"},
        {"phase14_missing_init_trigger_call", "", fixtures / "phase14_missing_init_trigger_call.expected.j"},
        {"phase14_struct_oninit_dependency_order", "", fixtures / "phase14_struct_oninit_dependency_order.expected.j"},
        {"phase14_struct_warn", "-warn", fixtures / "phase14_struct_warn.expected.j"},
        {"phase14_zinc_chain_comments_static_field", "", fixtures / "phase14_zinc_chain_comments_static_field.expected.j"},
        {"phase14_zinc_call_result_destroy", "", fixtures / "phase14_zinc_call_result_destroy.expected.j"},
        {"phase14_zinc_standalone_leading_dot_chain", "", fixtures / "phase14_zinc_standalone_leading_dot_chain.expected.j"},
        {"phase15_mode_fast_cli", "--mode fast", fixtures / "phase15_mode_fast_cli.expected.j"},
        {"phase15_bare_field_fastpath", "", fixtures / "phase15_bare_field_fastpath.expected.j"},
        {"phase15_static_method_fastpath", "", fixtures / "phase15_static_method_fastpath.expected.j"},
        {"phase15_local_shadow_fastpath", "", fixtures / "phase15_local_shadow_fastpath.expected.j"},
        {"phase16_struct_generated_support", "", fixtures / "phase16_struct_generated_support.expected.j"},
        {"phase16_struct_source_method_lowering", "", fixtures / "phase16_struct_source_method_lowering.expected.j"},
        {"phase16_struct_on_destroy", "", fixtures / "phase16_struct_on_destroy.expected.j"},
        {"phase16_struct_deallocate_direct", "", fixtures / "phase16_struct_deallocate_direct.expected.j"},
        {"phase18_body_mode_zinc_function", "", fixtures / "phase18_body_mode_zinc_function.expected.j"},
        {"phase18_body_mode_vjass_function", "", fixtures / "phase18_body_mode_vjass_function.expected.j"},
        {"phase18_body_mode_zinc_method", "", fixtures / "phase18_body_mode_zinc_method.expected.j"},
        {"phase18_body_mode_generated_support", "", fixtures / "phase18_body_mode_generated_support.expected.j"},
        {"phase18_zinc_inline_else_if_chain", "", fixtures / "phase18_zinc_inline_else_if_chain.expected.j"},
        {"phase18_zinc_else_if_after_comment", "", fixtures / "phase18_zinc_else_if_after_comment.expected.j"},
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
    ok = runExpectFail(exe, fixtures, outDir, "phase4_negative_signature_mismatch", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase4_negative_evaluate_on_nothing", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase4_negative_capturing_lambda", "") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase17_negative_bare_expression_statement", "--mode fast") && ok;
    ok = runExpectFail(exe, fixtures, outDir, "phase17_negative_zinc_member_literal", "--mode fast") && ok;

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

    stats = outDir / "phase4.stats.json";
    scan = exe.string() + " " + quote(fixtures / "phase4_function_interface_execute.in.j") +
           " --scan-only --allow-unsupported --emit-stats " + quote(stats);
    ok = runCommand(scan) && ok;
    statsText = readFile(stats);
    if (statsText.find("\"functionInterfaces\": 1") == std::string::npos ||
        statsText.find("\"functionInterfacesUnsupported\": 0") == std::string::npos) {
        std::cerr << "phase4 function-interface scan did not report expected counters\n";
        ok = false;
    }

    return ok ? 0 : 1;
}
