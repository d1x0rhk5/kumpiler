#include "gcc-plugin.h"
#include "plugin-version.h"

#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "context.h"
#include "function.h"
#include "gimple-iterator.h"
#include "diagnostic.h"

#include "gimple-pretty-print.h"
#include "cfgloop.h"

#include "gimple-pretty-print.h"
#include "print-tree.h"
#include "diagnostic-core.h"

#include "hash-set.h"

#include <cstring>
#include <string>
#include <vector>

int plugin_is_GPL_compatible;

static tree printf_decl = nullptr;

static bool is_array_element_assign(gimple *stmt)
{
    if (gimple_code(stmt) != GIMPLE_ASSIGN)
        return false;

    tree lhs = gimple_assign_lhs(stmt);
    if (TREE_CODE(lhs) != ARRAY_REF)
        return false;

    return true;
}

static gimple *build_printf_call_multi(const char *format_str, const std::vector<tree> &args_vec) {
    // 문자열 상수 트리 생성
    size_t len = strlen(format_str) + 1;
    tree str = build_string(len, format_str);
    TREE_TYPE(str) = build_array_type_nelts(char_type_node, len);

    // 문자열 포인터 트리 생성 (&"string")
    tree str_ptr = build1(ADDR_EXPR,
                          build_pointer_type_for_mode(char_type_node, ptr_mode, true),
                          str);

    // 인자 벡터 생성 및 포맷 문자열 포인터 추가
    vec<tree> args;
    args.create(args_vec.size() + 1);
    args.quick_push(str_ptr);  // 첫 인자는 포맷 문자열

    for (const tree &arg : args_vec)
        args.quick_push(arg);  // 나머지는 인자들

    // gimple call 생성
    return gimple_build_call_vec(printf_decl, args);
}

static gcall *build_exit_call(int exit_code) {
    tree exit_fn = builtin_decl_explicit(BUILT_IN_EXIT);
    tree code = build_int_cst(integer_type_node, exit_code);

    return gimple_build_call(exit_fn, 1, code);
}

gimple_stmt_iterator
create_cond_insert_point (gimple_stmt_iterator *iter,
			  bool before_p,
			  bool then_more_likely_p,
			  bool create_then_fallthru_edge,
			  basic_block *then_block,
			  basic_block *fallthrough_block)
{
  gimple_stmt_iterator gsi = *iter;

  if (!gsi_end_p (gsi) && before_p)
    gsi_prev (&gsi);

  basic_block cur_bb = gsi_bb (*iter);

  edge e = split_block (cur_bb, gsi_stmt (gsi));

  /* Get a hold on the 'condition block', the 'then block' and the
     'else block'.  */
  basic_block cond_bb = e->src;
  basic_block fallthru_bb = e->dest;
  basic_block then_bb = create_empty_bb (cond_bb);
  if (current_loops)
    {
      add_bb_to_loop (then_bb, cond_bb->loop_father);
      loops_state_set (LOOPS_NEED_FIXUP);
    }

  /* Set up the newly created 'then block'.  */
  e = make_edge (cond_bb, then_bb, EDGE_TRUE_VALUE);
  profile_probability fallthrough_probability
    = then_more_likely_p
    ? profile_probability::very_unlikely ()
    : profile_probability::very_likely ();
  e->probability = fallthrough_probability.invert ();
  then_bb->count = e->count ();
  if (create_then_fallthru_edge)
    make_single_succ_edge (then_bb, fallthru_bb, EDGE_FALLTHRU);

  /* Set up the fallthrough basic block.  */
  e = find_edge (cond_bb, fallthru_bb);
  e->flags = EDGE_FALSE_VALUE;
  e->probability = fallthrough_probability;

  /* Update dominance info for the newly created then_bb; note that
     fallthru_bb's dominance info has already been updated by
     split_bock.  */
  if (dom_info_available_p (CDI_DOMINATORS))
    set_immediate_dominator (CDI_DOMINATORS, then_bb, cond_bb);

  *then_block = then_bb;
  *fallthrough_block = fallthru_bb;
  *iter = gsi_start_bb (fallthru_bb);

  return gsi_last_bb (cond_bb);
}

static void
insert_if_then_before_iter (gcond *cond,
			    gimple_stmt_iterator *iter,
			    bool then_more_likely_p,
			    basic_block *then_bb,
			    basic_block *fallthrough_bb)
{
  gimple_stmt_iterator cond_insert_point =
    create_cond_insert_point (iter,
			      /*before_p=*/true,
			      then_more_likely_p,
			      /*create_then_fallthru_edge=*/true,
			      then_bb,
			      fallthrough_bb);
  gsi_insert_after (&cond_insert_point, cond, GSI_NEW_STMT);
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

        //중복 삽입 방지
        static hash_set<gimple *> visited;

        basic_block bb;
        FOR_EACH_BB_FN(bb, fun) {
            for (gimple_stmt_iterator gsi = gsi_start_bb(bb); !gsi_end_p(gsi); ) {
                gimple *stmt = gsi_stmt(gsi);

                if (visited.contains(stmt)) {
                    gsi_next(&gsi);
                    continue;
                }
                visited.add(stmt);

                print_gimple_stmt(stderr, stmt, 0, TDF_NONE);
                fprintf(stderr, "\n");
                
                if (is_array_element_assign(stmt)) {
                    tree lhs = gimple_assign_lhs(stmt);
                    tree idx = TREE_OPERAND(lhs, 1);  
                    tree arr = TREE_OPERAND(lhs, 0);    

                    if (TREE_CODE(idx) == INTEGER_CST) {
                        HOST_WIDE_INT iv  = TREE_INT_CST_LOW(idx);
                        tree arr_ty  = TREE_TYPE(arr);              
                        tree domain  = TYPE_DOMAIN(arr_ty);         
                        tree tmin    = TYPE_MIN_VALUE(domain);
                        tree tmax    = TYPE_MAX_VALUE(domain);
                        HOST_WIDE_INT min = TREE_INT_CST_LOW(tmin);
                        HOST_WIDE_INT max = TREE_INT_CST_LOW(tmax);
                        if (iv < min || iv > max) {
                            error_at (gimple_location(stmt), "array index %ld out of bounds [%ld~%ld]", iv, min, max);
                        }
                    }
                    else if (TREE_CODE(idx) == SSA_NAME || TREE_CODE(idx) == VAR_DECL) {
                        tree arr_ty = TREE_TYPE(arr);              
                        tree domain = TYPE_DOMAIN(arr_ty);         
                        tree tmin = TYPE_MIN_VALUE(domain); 
                        tree tmax = TYPE_MAX_VALUE(domain); 

                        HOST_WIDE_INT min = TREE_INT_CST_LOW(tmin);
                        HOST_WIDE_INT max = TREE_INT_CST_LOW(tmax);

                        tree min_tree = build_int_cst(NULL_TREE, min);
                        tree max_tree = build_int_cst(NULL_TREE, max);

                        location_t loc = gimple_location (stmt);

                        // 조건 1: idx < 0
                        {
                            gcond *cond_stmt = gimple_build_cond(LT_EXPR, idx, min_tree, NULL_TREE, NULL_TREE);
                            gimple_set_location(cond_stmt, loc);

                            basic_block then_bb, fallthrough_bb;
                            insert_if_then_before_iter(cond_stmt, &gsi, true, &then_bb, &fallthrough_bb);

                            gsi = gsi_start_bb(then_bb);
                            
                            std::vector<tree> printf_args = {idx, min_tree};
                            gimple *error_print_stmt = build_printf_call_multi("Error: Array index %d is less than minimum index %d.\n", printf_args);
                            gsi_insert_before(&gsi, error_print_stmt, GSI_SAME_STMT);

                            gcall *exit_stmt = build_exit_call(1);
                            gsi_insert_before(&gsi, exit_stmt, GSI_SAME_STMT);

                            gsi = gsi_start_bb(fallthrough_bb);
                        }
                        
                        // 조건 2: idx > tmax
                        {
                            gcond *cond_stmt = gimple_build_cond(GT_EXPR, idx, max_tree, NULL_TREE, NULL_TREE);
                            gimple_set_location(cond_stmt, loc);

                            basic_block then_bb, fallthrough_bb;
                            insert_if_then_before_iter(cond_stmt, &gsi, true, &then_bb, &fallthrough_bb);

                            gsi = gsi_start_bb(then_bb);

                            std::vector<tree> printf_args = {idx, max_tree};
                            gimple *error_print_stmt = build_printf_call_multi("Error: Array index %d is less than minimum index %d.\n", printf_args);
                            gsi_insert_before(&gsi, error_print_stmt, GSI_SAME_STMT);

                            gcall *exit_stmt = build_exit_call(1);
                            gsi_insert_before(&gsi, exit_stmt, GSI_SAME_STMT);

                            gsi = gsi_start_bb(fallthrough_bb);
                        }
                    }
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
