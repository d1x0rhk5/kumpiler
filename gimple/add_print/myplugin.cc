#include "gcc-plugin.h"
#include "plugin-version.h"

#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "context.h"
#include "function.h"
#include "gimple-iterator.h"

#include <cstring>
#include <string>

int plugin_is_GPL_compatible;

static tree printf_decl = nullptr;

// 문자열 상수 생성
static tree build_string_const(const char *str) {
    return build_string_literal(strlen(str) + 1, str);
}

// printf("문자열") 호출문 생성
static gimple *build_printf_call(const char *msg) {
    size_t len = strlen(msg) + 1;
    tree str = build_string(len, msg);
    TREE_TYPE(str) = build_array_type_nelts(char_type_node, len);

    tree str_ptr = build1(ADDR_EXPR,
                          build_pointer_type_for_mode(char_type_node, ptr_mode, true),
                          str);

    vec<tree> *args = nullptr;
    vec_alloc(args, 1);
    args->quick_push(str_ptr);

    return gimple_build_call_vec(printf_decl, *args);
}

// printf 호출인지 확인
static bool is_printf_call(gimple *stmt) {
    if (!is_gimple_call(stmt))
        return false;

    tree fn = gimple_call_fn(stmt);
    if (!fn) {
        fprintf(stderr, "[is_printf_call] No function found in call stmt\n");
        return false;
    }

    tree decl = nullptr;

    // 다양한 형태 지원: ADDR_EXPR (e.g., &printf), FUNCTION_DECL (e.g., printf)
    switch (TREE_CODE(fn)) {
        case ADDR_EXPR:
            decl = TREE_OPERAND(fn, 0);
            break;
        case FUNCTION_DECL:
            decl = fn;
            break;
        default:
            fprintf(stderr, "[is_printf_call] Unsupported TREE_CODE: %d\n",
                    TREE_CODE(fn));
            return false;
    }

    if (!decl || !DECL_NAME(decl)) {
        fprintf(stderr, "[is_printf_call] Function declaration or name missing\n");
        return false;
    }

    const char *name = IDENTIFIER_POINTER(DECL_NAME(decl));

    // 디버깅 로그: 호출되는 함수 이름 출력
    //fprintf(stderr, "[is_printf_call] Detected call to: %s\n", name);

    // gimeple에서 최적화 처리로 printf가 이름이 바뀔 수 있음
    return strcmp(name, "printf") == 0 ||
           strcmp(name, "__builtin_printf") == 0 ||
           strcmp(name, "__printf") == 0 ||
           strcmp(name, "__builtin_puts") == 0;
}


// 중복 삽입 방지
static bool is_injected_string_call(gimple *stmt, const char *msg) {
    if (!is_printf_call(stmt)) return false;
    if (gimple_call_num_args(stmt) < 1) return false;

    tree arg = gimple_call_arg(stmt, 0);

    if (TREE_CODE(arg) != ADDR_EXPR) return false;
    tree str = TREE_OPERAND(arg, 0);
    const char *p = TREE_STRING_POINTER(str);
   
    std::string target = std::string(msg);
    return p && strcmp(p, target.c_str()) == 0;
}

namespace {

const pass_data my_pass_data = {
    GIMPLE_PASS,
    "insert_printf_hooks",  // 패스 이름
    OPTGROUP_NONE,
    TV_NONE,
    0, 0, 0, 0, 0
};

struct my_pass : gimple_opt_pass {
    my_pass(gcc::context *ctxt)
        : gimple_opt_pass(my_pass_data, ctxt) {}

    unsigned int execute(function *fun) override {
        if (!printf_decl) {
            printf_decl = builtin_decl_implicit(BUILT_IN_PRINTF);
            if (!printf_decl) {
                perror("printf not declared");
                return 0;
            }
        }

        basic_block bb;
        FOR_EACH_BB_FN(bb, fun) {
            for (gimple_stmt_iterator gsi = gsi_start_bb(bb); !gsi_end_p(gsi); ) {
                gimple *stmt = gsi_stmt(gsi);

                if (is_printf_call(stmt)) {
                    if (is_injected_string_call(stmt, "[-----printf start-----]\n") ||
                        is_injected_string_call(stmt, "[-----printf  end -----]\n")) {
                        gsi_next(&gsi);
                        continue;
                    }

                    gimple_stmt_iterator orig = gsi;
                    //gsi_remove(&gsi, true); //코드 대치용

                    gimple *before = build_printf_call("[-----printf start-----]\n");
                    gsi_insert_before(&gsi, before, GSI_SAME_STMT);

                    gsi_next(&gsi); //코드 대치시 제거 앞 뒤 삽입시에는 주석 해제

                    gimple *after = build_printf_call("[-----printf  end -----]\n");
                    if (!gsi_end_p(gsi)) {
                        gsi_insert_before(&gsi, after, GSI_SAME_STMT);
                    } else {
                        gsi_insert_after(&orig, after, GSI_NEW_STMT);
                    }

                    continue;
                }

                gsi_next(&gsi);
            }
        }
        return 0;
    }
};

} // namespace

int plugin_init(struct plugin_name_args *plugin_info,
                struct plugin_gcc_version *version) {
    struct register_pass_info pass_info = {
        .pass = new my_pass(g),
        .reference_pass_name = "cfg",
        .ref_pass_instance_number = 1,
        .pos_op = PASS_POS_INSERT_AFTER
    };

    register_callback(plugin_info->base_name,
                      PLUGIN_PASS_MANAGER_SETUP,
                      nullptr,
                      &pass_info);
    return 0;
}
