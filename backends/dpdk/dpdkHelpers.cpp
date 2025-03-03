/*
Copyright 2020 Intel Corp.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <iostream>
#include "dpdkHelpers.h"
#include "ir/ir.h"
#include "frontends/p4/tableApply.h"

namespace DPDK {

// convert relation comparison statements into the corresponding branching
// instructions in dpdk.
void ConvertStatementToDpdk::process_relation_operation(const IR::Expression* dst,
                                                        const IR::Operation_Relation* op) {
    auto true_label = refmap->newName("label_true");
    auto false_label = refmap->newName("label_false");
    auto end_label = refmap->newName("label_end");
    cstring label1 = false_label;
    cstring label2 = true_label;
    bool condNegated = false;
    if (op->is<IR::Equ>()) {
        add_instr(new IR::DpdkJmpEqualStatement(true_label, op->left, op->right));
    } else if (op->is<IR::Neq>()) {
        add_instr(new IR::DpdkJmpNotEqualStatement(true_label, op->left, op->right));
    } else if (op->is<IR::Lss>()) {
        add_instr(new IR::DpdkJmpLessStatement(true_label, op->left, op->right));
    } else if (op->is<IR::Grt>()) {
        add_instr(new IR::DpdkJmpGreaterStatement(true_label, op->left, op->right));
    } else if (op->is<IR::Leq>()) {
        /* Dpdk target does not support the condition Leq, so negate the condition and jump
           on false label*/
        condNegated = true;
        add_instr(new IR::DpdkJmpGreaterStatement(false_label, op->left, op->right));
    } else if (op->is<IR::Geq>()) {
        /* Dpdk target does not support the condition Geq, so negate the condition and jump
           on false label*/
        condNegated = true;
        add_instr(new IR::DpdkJmpLessStatement(false_label, op->left, op->right));
    } else {
        BUG("%1% not implemented.", op);
    }
    if (condNegated) {
        // Since the condition is negated, true and false blocks should also be swapped
        label1 = true_label;
        label2 = false_label;
    }

    add_instr(new IR::DpdkLabelStatement(label1));
    add_instr(new IR::DpdkMovStatement(dst, new IR::Constant(condNegated)));
    add_instr(new IR::DpdkJmpLabelStatement(end_label));
    add_instr(new IR::DpdkLabelStatement(label2));
    add_instr(new IR::DpdkMovStatement(dst, new IR::Constant(!condNegated)));
    add_instr(new IR::DpdkLabelStatement(end_label));
}

/* DPDK target does not support storing result of logical operations such as || and &&.
   Hence we convert the expression var = a LOP b into if-else statements.
   var = a || b            ||     var = a && b
=> if (a){                 ||=>   if (!a)
       var = true;         ||         var = false;
   } else {                ||     } else {
       if (b)              ||         if (!b)
           var = true;     ||             var = false;
       else                ||         else
           var = false;    ||             var = true;
   }                       ||     }

   This function assumes complex logical operations are converted to simple expressions by
   ConvertLogicalExpression pass.
*/
void ConvertStatementToDpdk::process_logical_operation(const IR::Expression* dst,
                                                        const IR::Operation_Binary* op) {
    if (!op->is<IR::LOr>() && !op->is<IR::LAnd>())
        return;
    auto true_label = refmap->newName("label_true");
    auto false_label = refmap->newName("label_false");
    auto end_label = refmap->newName("label_end");
    if (op->is<IR::LOr>()) {
        add_instr(new IR::DpdkJmpEqualStatement(true_label, op->left, new IR::Constant(true)));
        add_instr(new IR::DpdkJmpEqualStatement(true_label, op->right, new IR::Constant(true)));
        add_instr(new IR::DpdkMovStatement(dst, new IR::Constant(false)));
        add_instr(new IR::DpdkJmpLabelStatement(end_label));
        add_instr(new IR::DpdkLabelStatement(true_label));
        add_instr(new IR::DpdkMovStatement(dst, new IR::Constant(true)));
        add_instr(new IR::DpdkLabelStatement(end_label));
    } else if (op->is<IR::LAnd>()) {
        add_instr(new IR::DpdkJmpEqualStatement(false_label, op->left, new IR::Constant(false)));
        add_instr(new IR::DpdkJmpEqualStatement(false_label, op->right, new IR::Constant(false)));
        add_instr(new IR::DpdkMovStatement(dst, new IR::Constant(true)));
        add_instr(new IR::DpdkJmpLabelStatement(end_label));
        add_instr(new IR::DpdkLabelStatement(false_label));
        add_instr(new IR::DpdkMovStatement(dst, new IR::Constant(false)));
        add_instr(new IR::DpdkLabelStatement(end_label));
    } else {
        BUG("%1% not implemented.", op);
    }
}

bool ConvertStatementToDpdk::preorder(const IR::AssignmentStatement *a) {
    auto left = a->left;
    auto right = a->right;

    IR::DpdkAsmStatement *i = nullptr;

    if (auto r = right->to<IR::Operation_Relation>()) {
        process_relation_operation(left, r);
    } else if (auto r = right->to<IR::Operation_Binary>()) {
        if (right->is<IR::Add>()) {
            i = new IR::DpdkAddStatement(left, r->left, r->right);
        } else if (right->is<IR::Sub>()) {
            i = new IR::DpdkSubStatement(left, r->left, r->right);
        } else if (right->is<IR::Shl>()) {
            i = new IR::DpdkShlStatement(left, r->left, r->right);
        } else if (right->is<IR::Shr>()) {
            i = new IR::DpdkShrStatement(left, r->left, r->right);
        } else if (right->is<IR::Equ>()) {
            i = new IR::DpdkEquStatement(left, r->left, r->right);
        } else if (right->is<IR::LOr>() || right->is<IR::LAnd>()) {
            process_logical_operation(left, r);
        } else if (right->is<IR::BOr>()) {
            i = new IR::DpdkOrStatement(left, r->left, r->right);
        } else if (right->is<IR::BAnd>()) {
            i = new IR::DpdkAndStatement(left, r->left, r->right);
        } else if (right->is<IR::BXor>()) {
            i = new IR::DpdkXorStatement(left, r->left, r->right);
        } else {
            BUG("%1% not implemented.", right);
        }
    } else if (auto m = right->to<IR::MethodCallExpression>()) {
        auto mi = P4::MethodInstance::resolve(m, refmap, typemap);
        if (auto e = mi->to<P4::ExternMethod>()) {
            if (e->originalExternType->getName().name == "Hash") {
                if (e->expr->arguments->size() == 1) {
                    auto field = (*e->expr->arguments)[0];
                    i = new IR::DpdkGetHashStatement(e->object->getName(),
                                                     field->expression, left);
                }
            } else if (e->originalExternType->getName().name ==
                       "InternetChecksum") {
                if (e->method->getName().name == "get") {
                    auto res = structure->csum_map.find(
                        e->object->to<IR::Declaration_Instance>());
                    cstring intermediate;
                    if (res != structure->csum_map.end()) {
                        intermediate = res->second;
                    }
                    i = new IR::DpdkGetChecksumStatement(
                        left, e->object->getName(), intermediate);
                }
            } else if (e->originalExternType->getName().name == "Meter") {
                if (e->method->getName().name == "execute") {
                    auto argSize = e->expr->arguments->size();

                    // DPDK target needs index and packet length as mandatory parameters
                    if (argSize < 2) {
                        ::error(ErrorType::ERR_UNEXPECTED, "Expected atleast 2 arguments for %1%",
                                e->object->getName());
                        return false;
                    }
                    const IR::Expression *color_in = nullptr;
                    const IR::Expression *length = nullptr;
                    auto index = e->expr->arguments->at(0)->expression;
                    if (argSize == 2) {
                        length = e->expr->arguments->at(1)->expression;
                        color_in = new IR::Constant(1);
                    } else if (argSize == 3) {
                        length = e->expr->arguments->at(2)->expression;
                        color_in = e->expr->arguments->at(1)->expression;
                    }
                    i = new IR::DpdkMeterExecuteStatement(
                         e->object->getName(), index, length, color_in, left);
                }
            } else if (e->originalExternType->getName().name == "Register") {
                if (e->method->getName().name == "read") {
                    auto index = (*e->expr->arguments)[0]->expression;
                    i = new IR::DpdkRegisterReadStatement(
                        left, e->object->getName(), index);
                }
            } else if (e->originalExternType->getName().name == "packet_in") {
                if (e->method->getName().name == "lookahead") {
                    i = new IR::DpdkLookaheadStatement(left);
                }
            } else {
                BUG("%1% Not implemented", e->originalExternType->name);
            }
        } else if (auto e = mi->to<P4::ExternFunction>()) {
            /* PNA SelectByDirection extern is implemented as
                SelectByDirection<T>(in PNA_Direction_t direction, in T n2h_value, in T h2n_value) {
                    if (direction == PNA_Direction_t.NET_TO_HOST) {
                        return n2h_value;
                    } else {
                        return h2n_value;
                    }
                }
                Example:
                    table ipv4_da_lpm {
                        key = {
                            SelectByDirection(istd.direction, hdr.ipv4.srcAddr, hdr.ipv4.dstAddr):
                                                                          lpm @name ("ipv4_addr");
                        }
                        ....
                }

                In this example, KeySideEffect pass inserts a temporary for holding the result of
                complex key expression and replaces the key expression with the temporary. An
                assignment is inserted in the apply block before the table apply to assign the complex
                expression into this temporary variable. After KeySideEffect pass, SelectByDirection
                extern invocation is present as RHS of assignment and hence is evaluated here in this
                visitor.

                After keySideEffect pass:
                bit <> key_0;
                ...
                table ipv4_da_lpm {
                    key = {
                        key_0: lpm @name ("ipv4_addr");
                    }
                    ....
                }

                apply {
                    key_0 =
                    SelectByDirection<bit<32>>(istd.direction, hdr.ipv4.srcAddr, hdr.ipv4.dstAddr);
                    ...
		    ...
                }

                This is replaced by an assignment of the resultant value into a temporary based on
                the direction. Assembly equivalent to the below code is emitted:

                if (istd.direction == PNA_Direction_t.NET_TO_HOST) {
                    key_0 = hdr.ipv4.srcAddr;
                } else {
                    key_0 = hdr.ipv4.dstAddr;
                }

                The equivalent assembly looks like this:
                jmpeq LABEL_TRUE_0 m.pna_main_input_metadata_direction 0x0
                mov m.<controlBlockName>_key_0 h.ipv4.dstAddr
                jmp LABEL_END_0
                LABEL_TRUE_0 : mov m.<controlBlockName>_key_0 h.ipv4.srcAddr
                LABEL_END_0 : ...
            */
            if (e->method->name == "SelectByDirection") {
                auto args = e->expr->arguments;
                auto dir = args->at(0)->expression;
                auto firstVal = args->at(1)->expression;
                auto secondVal = args->at(2)->expression;
                auto true_label = refmap->newName("label_true");
                auto end_label = refmap->newName("label_end");

                /* Emit jump to block containing assignment for PNA_Direction_t.NET_TO_HOST */
                add_instr(new IR::DpdkJmpEqualStatement(true_label, dir, new IR::Constant(0)));
                add_instr(new IR::DpdkMovStatement(left, secondVal));
                add_instr(new IR::DpdkJmpLabelStatement(end_label));
                add_instr(new IR::DpdkLabelStatement(true_label));
                add_instr(new IR::DpdkMovStatement(left, firstVal));
                i = new IR::DpdkLabelStatement(end_label);
            } else {
                BUG("%1% Not Implemented", e->method->name);
            }
        } else if (auto b = mi->to<P4::BuiltInMethod>()) {
            if (b->name == "isValid") {
                /* DPDK target does not support isvalid() method call as RHS of assignment
                   Hence, var = hdr.isValid() is translated as
                   var = true;
                   if (!(hdr.isValid()))
                       var = false;
                */
                auto end_label = refmap->newName("label_end");
                add_instr(new IR::DpdkMovStatement(left, new IR::BoolLiteral(true)));
                add_instr(new IR::DpdkJmpIfValidStatement(end_label, b->appliedTo));
                add_instr(new IR::DpdkMovStatement(left, new IR::BoolLiteral(false)));
                i = new IR::DpdkLabelStatement(end_label);
            } else {
                BUG("%1% Not Implemented", b->name);
            }
        } else {
            BUG("%1% Not implemented", m);
        }
    } else if (right->is<IR::Operation_Unary>() && !right->is<IR::Member>()) {
        if (auto ca = right->to<IR::Cast>()) {
            i = new IR::DpdkCastStatement(left, ca->expr, ca->destType);
        } else if (auto n = right->to<IR::Neg>()) {
            i = new IR::DpdkNegStatement(left, n->expr);
        } else if (auto c = right->to<IR::Cmpl>()) {
            i = new IR::DpdkCmplStatement(left, c->expr);
        } else if (auto ln = right->to<IR::LNot>()) {
            /* DPDK target does not support storing result of logical NOT operation.
               Hence we convert the expression var = !a into if-else statement.
               if (a == 0)
                   var = 1;
               else
                   var = 0;
            */
            auto true_label = refmap->newName("label_true");
            auto end_label = refmap->newName("label_end");
            add_instr(new IR::DpdkJmpEqualStatement(true_label, ln->expr, new IR::Constant(false)));
            add_instr(new IR::DpdkMovStatement(left, new IR::Constant(false)));
            add_instr(new IR::DpdkJmpLabelStatement(end_label));
            add_instr(new IR::DpdkLabelStatement(true_label));
            add_instr(new IR::DpdkMovStatement(left, new IR::Constant(true)));
            i = new IR::DpdkLabelStatement(end_label);
        } else {
            std::cerr << right->node_type_name() << std::endl;
            BUG("Not implemented.");
        }
    } else if (right->is<IR::PathExpression>() || right->is<IR::Member>() ||
               right->is<IR::BoolLiteral>() || right->is<IR::Constant>()) {
        i = new IR::DpdkMovStatement(a->left, a->right);
    } else {
        std::cerr << right->node_type_name() << std::endl;
        BUG("Not implemented.");
    }
    if (i)
        add_instr(i);
    return false;
}

/* This recursion requires the pass of ConvertLogicalExpression. This pass will
 * transform the logical experssion to a form that this function use as
 * presumption. The presumption of this function is that the left side of
 * the logical expression can be a simple expression(expression that is not
 *  LAnd or LOr) or a nested expression(LAnd or LOr). The right side can be a
 * nested expression or {simple one if left side is simple as well}.
 */
bool BranchingInstructionGeneration::generate(const IR::Expression *expr,
        cstring true_label,
        cstring false_label,
        bool is_and) {
    if (auto land = expr->to<IR::LAnd>()) {
        /* First, the left side and right side are both nested expressions. In
         * this case, we need to introduce another label that represent the half
         * truth for LAnd(means the left side of LAnd is true).And what will
         * fall through depends on the return value of recursion for right side.
         */
        if (nested(land->left) && nested(land->right)) {
            generate(land->left, true_label + "half", false_label, true);
            instructions.push_back(
                    new IR::DpdkLabelStatement(true_label + "half"));
            return generate(land->right, true_label, false_label, true);
        } else if (!nested(land->left) && nested(land->right)) {
        /* Second, left is simple and right is nested. Call recursion for the
         * left part, true fall through. Note that right now, the truthfulness
         * of right represents the truthfulness of the whole, because if the
         * left part is false, it will not ranch to the right part due to
         * principle of short-circuit. For right side, recursion is called and
         * what will fall through depends on the return value of this recursion
         * function. Since the basic case and `left simple right simple` case
         * have already properly jmp to correct label, there is no need to jmp
         * to any label.
         */
            generate(land->left, true_label, false_label, true);
            return generate(land->right, true_label, false_label, true);
        } else if (!nested(land->left) && !nested(land->right)) {
        /* Third, left is simple and right is simple. In this case, call the
         * recursion and indicating  that this call is from LAnd, the
         * subfunction will let true fall  through. Therefore, after two
         * function calls, the control flow  should jump to true label. In
         * addition, indicate that true condition  fallen through. The reason
         * why I still need to indicate true fallen  through is because there
         * is chance to eliminate the jmp instruction  I just added.
         */
            generate(land->left, true_label, false_label, true);
            generate(land->right, true_label, false_label, true);
            instructions.push_back(new IR::DpdkJmpLabelStatement(true_label));
            return true;
        } else {
            BUG("Previous simple expression lifting pass failed");
        }
    } else if (auto lor = expr->to<IR::LOr>()) {
        if (nested(lor->left) && nested(lor->right)) {
            generate(lor->left, true_label, false_label + "half", false);
            instructions.push_back(
                    new IR::DpdkLabelStatement(false_label + "half"));
            return generate(lor->right, true_label, false_label, false);
        } else if (!nested(lor->left) && nested(lor->right)) {
            generate(lor->left, true_label, false_label, false);
            return generate(lor->right, true_label, false_label, false);
        } else if (!nested(lor->left) && !nested(lor->right)) {
            generate(lor->left, true_label, false_label, false);
            generate(lor->right, true_label, false_label, false);
            instructions.push_back(new IR::DpdkJmpLabelStatement(false_label));
            return false;
        } else {
            BUG("Previous simple expression lifting pass failed");
        }
    } else if (auto equ = expr->to<IR::Equ>()) {
    /* First, I will describe the base case. The base case is handling logical
     * expression that is not LAnd and LOr. It will conside whether the simple
     * expression itself is the left or right of a LAnd or a LOr(The
     * information is provided by is_and). If it is from LAnd, it will use the
     * opposite branching statement and let true fall through. For example, a
     * == b becomes jneq a b false. This is because for a LAnd if one of its
     * statement is true it is not necessary to be true, but if one is false,
     * it is definitely false. If it is from a LOr, it will let false fall
     * through. And finally for a base case, it returns what condition it fall
     * through.
     */
        if (is_and) {
            instructions.push_back(new IR::DpdkJmpNotEqualStatement(
                        false_label, equ->left, equ->right));
        } else {
            instructions.push_back(new IR::DpdkJmpEqualStatement(
                        true_label, equ->left, equ->right));
        }
        return is_and;
    } else if (auto neq = expr->to<IR::Neq>()) {
        if (is_and) {
            instructions.push_back(new IR::DpdkJmpEqualStatement(
                        false_label, neq->left, neq->right));
        } else {
            instructions.push_back(new IR::DpdkJmpNotEqualStatement(
                        true_label, neq->left, neq->right));
        }
        return is_and;
    } else if (auto lss = expr->to<IR::Lss>()) {
        /* Dpdk target does not support the negated condition Geq,
           so always jump on true label*/
        instructions.push_back(new IR::DpdkJmpLessStatement(
                     true_label, lss->left, lss->right));
        return false;
    } else if (auto grt = expr->to<IR::Grt>()) {
        /* Dpdk target does not support the negated condition Leq,
           so always jump on true label*/
        instructions.push_back(new IR::DpdkJmpGreaterStatement(
                     true_label, grt->left, grt->right));
        return false;
    } else if (auto geq = expr->to<IR::Geq>()) {
        /* Dpdk target does not support the condition Geq,
           so always negate the condition and jump on false label*/
        instructions.push_back(new IR::DpdkJmpLessStatement(
                     false_label, geq->left, geq->right));
        return true;
    } else if (auto leq = expr->to<IR::Leq>()) {
        /* Dpdk target does not support the condition Leq,
           so always negate the condition and jump on false label*/
        instructions.push_back(new IR::DpdkJmpGreaterStatement(
                     false_label, leq->left, leq->right));
        return true;
    } else if (auto mce = expr->to<IR::MethodCallExpression>()) {
        auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
        if (auto a = mi->to<P4::BuiltInMethod>()) {
            if (a->name == "isValid") {
                if (is_and) {
                    instructions.push_back(new IR::DpdkJmpIfInvalidStatement(
                                false_label, a->appliedTo));
                } else {
                    instructions.push_back(new IR::DpdkJmpIfValidStatement(
                                true_label, a->appliedTo)); }
                return is_and;
            } else {
                BUG("%1%: Not implemented", expr);
            }
        } else {
            BUG("%1%:not implemented method instance", expr);
        }
    } else if (expr->is<IR::PathExpression>()) {
        if (is_and) {
            instructions.push_back(new IR::DpdkJmpNotEqualStatement(
                        false_label, expr, new IR::Constant(1)));
        } else {
            instructions.push_back(new IR::DpdkJmpEqualStatement(
                        true_label, expr, new IR::Constant(1))); }
    } else if (auto mem = expr->to<IR::Member>()) {
        if (auto mce = mem->expr->to<IR::MethodCallExpression>()) {
            auto mi = P4::MethodInstance::resolve(mce, refMap, typeMap);
            if (auto a = mi->to<P4::ApplyMethod>()) {
                if (a->isTableApply()) {
                    if (mem->member == IR::Type_Table::hit) {
                        auto tbl = a->object->to<IR::P4Table>();
                        instructions.push_back(
                                new IR::DpdkApplyStatement(tbl->name.toString()));
                        if (is_and) {
                            instructions.push_back(
                                    new IR::DpdkJmpMissStatement(false_label));
                        } else {
                            instructions.push_back(
                                    new IR::DpdkJmpHitStatement(true_label));
                        }
                    }
                } else {
                    BUG("%1%: not implemented.", expr);
                }
            } else {
                BUG("%1%: not implemented.", expr);
            }
        } else {
            if (is_and) {
                instructions.push_back(new IR::DpdkJmpNotEqualStatement(
                            false_label, expr, new IR::Constant(1)));
            } else {
                instructions.push_back(new IR::DpdkJmpEqualStatement(
                            true_label, expr, new IR::Constant(1))); }
        }
        return is_and;
    } else if (auto lnot = expr->to<IR::LNot>()) {
        generate(lnot->expr, false_label, true_label, false);
        return is_and;
    } else {
        BUG("%1%: not implemented", expr);
    }
    return is_and;
}

// This function convert IfStatement to dpdk asm. Based on the return value of
// BranchingInstructionGeneration's recursion function, this function decides
// whether the true or false code block will go first. This is important because
// following optimization pass might eliminate some redundant jmps and labels.
bool ConvertStatementToDpdk::preorder(const IR::IfStatement *s) {
    auto true_label = refmap->newName("label_true");
    auto false_label = refmap->newName("label_false");
    auto end_label = refmap->newName("label_end");
    auto gen = new BranchingInstructionGeneration(refmap, typemap);
    bool res = gen->generate(s->condition, true_label, false_label, true);

    instructions.append(gen->instructions);
    if (res == true) {
        add_instr(new IR::DpdkLabelStatement(true_label));
        visit(s->ifTrue);
        add_instr(new IR::DpdkJmpLabelStatement(end_label));
        add_instr(new IR::DpdkLabelStatement(false_label));
        visit(s->ifFalse);
        add_instr(new IR::DpdkLabelStatement(end_label));
    } else {
        add_instr(new IR::DpdkLabelStatement(false_label));
        visit(s->ifFalse);
        add_instr(new IR::DpdkJmpLabelStatement(end_label));
        add_instr(new IR::DpdkLabelStatement(true_label));
        visit(s->ifTrue);
        add_instr(new IR::DpdkLabelStatement(end_label));
    }
    return false;
}

cstring ConvertStatementToDpdk::append_parser_name(const IR::P4Parser* p, cstring label) {
    return p->name + "_" + label;
}

bool ConvertStatementToDpdk::preorder(const IR::MethodCallStatement *s) {
    auto mi = P4::MethodInstance::resolve(s->methodCall, refmap, typemap);
    if (auto a = mi->to<P4::ApplyMethod>()) {
        LOG3("apply method: " << dbp(s) << std::endl << s);
        if (a->isTableApply()) {
            auto table = a->object->to<IR::P4Table>();
            add_instr(new IR::DpdkApplyStatement(table->name.toString()));
        } else {
            BUG("not implemented for `apply` other than table");
        }
    } else if (auto a = mi->to<P4::ExternMethod>()) {
        LOG3("extern method: " << dbp(s) << std::endl << s);
        // Checksum function call
        if (a->originalExternType->getName().name == "InternetChecksum") {
            auto res =
                structure->csum_map.find(a->object->to<IR::Declaration_Instance>());
            cstring intermediate;
            if (res != structure->csum_map.end()) {
                intermediate = res->second;
            } else {
                BUG("checksum map does not collect all checksum def.");
            }

            if (a->method->getName().name == "add") {
                auto args = a->expr->arguments;
                const IR::Argument *arg = args->at(0);
                if (auto l = arg->expression->to<IR::ListExpression>()) {
                    for (auto field : l->components) {
                        add_instr(new IR::DpdkChecksumAddStatement(
                            a->object->getName(), intermediate, field));
                    }
                } else if (auto s = arg->expression->to<IR::StructExpression>()) {
                    for (auto field : s->components) {
                        add_instr(new IR::DpdkChecksumAddStatement(
                            a->object->getName(), intermediate, field->expression));
                    }
                } else {
                    add_instr(new IR::DpdkChecksumAddStatement(
                                a->object->getName(), intermediate, arg->expression));
                }
            } else if (a->method->getName().name == "subtract") {
                auto args = a->expr->arguments;
                const IR::Argument *arg = args->at(0);
                if (auto l = arg->expression->to<IR::ListExpression>()) {
                    for (auto field : l->components) {
                        add_instr(new IR::DpdkChecksumSubStatement(
                            a->object->getName(), intermediate, field));
                    }
                } else if (auto s = arg->expression->to<IR::StructExpression>()) {
                    for (auto field : s->components) {
                        add_instr(new IR::DpdkChecksumSubStatement(
                            a->object->getName(), intermediate, field->expression));
                    }
                } else {
                    add_instr(new IR::DpdkChecksumSubStatement(
                                a->object->getName(), intermediate, arg->expression));
                }
            } else if (a->method->getName().name == "clear") {
                add_instr(new IR::DpdkChecksumClearStatement(
                            a->object->getName(), intermediate));
            }
        } else if (a->originalExternType->getName().name == "packet_out") {
            if (a->method->getName().name == "emit") {
                auto args = a->expr->arguments;
                auto header = args->at(0);
                if (header->expression->is<IR::Member>() ||
                    header->expression->is<IR::PathExpression>() ||
                    header->expression->is<IR::ArrayIndex>()) {
                    add_instr(new IR::DpdkEmitStatement(header->expression));
                } else {
                    ::error(ErrorType::ERR_UNSUPPORTED, "%1% is not supported", s);
                }
            }
        } else if (a->originalExternType->getName().name == "packet_in") {
            if (a->method->getName().name == "extract") {
                auto args = a->expr->arguments;
                if (args->size() == 1) {
                    auto header = args->at(0);
                    if (header->expression->is<IR::Member>() ||
                        header->expression->is<IR::PathExpression>() ||
                        header->expression->is<IR::ArrayIndex>()) {
                        add_instr(new IR::DpdkExtractStatement(header->expression));
                    } else {
                        ::error(ErrorType::ERR_UNSUPPORTED, "%1% is not supported", s);
                    }
                } else if (args->size() == 2) {
                    auto header = args->at(0);
                    auto length = args->at(1);
                    /**
                     * Extract instruction of DPDK target expects the second argument
                     * (size of the varbit field of the header) to be the number of bytes
                     * to be extracted while in P4 it is the number of bits to be extracted.
                     * We need to compute the size in bytes.
                     *
                     * @warning If the value is not aligned to 8 bits, the remainder after
                     * division is dropped during runtime (this is a target limitation).
                     */
                    IR::ID tmpName(refmap->newName(
                            length->expression->to<IR::Member>()->member.name + "_extract_tmp"));
                    BUG_CHECK(metadataStruct, "Metadata structure missing unexpectedly!");
                    metadataStruct->fields.push_back(
                            new IR::StructField(tmpName, length->expression->type));
                    auto tmpMember = new IR::Member(new IR::PathExpression("m"), tmpName);
                    add_instr(new IR::DpdkMovStatement(tmpMember, length->expression));
                    add_instr(new IR::DpdkShrStatement(tmpMember, tmpMember,
                            new IR::PathExpression("0x3")));
                    add_instr(new IR::DpdkExtractStatement(header->expression, tmpMember));
                }
            }
        } else if (a->originalExternType->getName().name == "Meter") {
            if (a->method->getName().name == "execute") {
                // DPDK target requires the result of meter execute method is assigned to a
                // variable of PSA_MeterColor_t type.
                ::error(ErrorType::ERR_UNSUPPORTED, "LHS of meter execute statement is missing " \
                        "Use this format instead : color_out = %1%.execute(index, color_in)",
                         a->object->getName());
            } else {
                BUG("Meter function not implemented.");
            }
        } else if (a->originalExternType->getName().name == "Counter") {
            auto di = a->object->to<IR::Declaration_Instance>();
            auto declArgs = di->arguments;
            unsigned value = 0;
            auto counter_type = declArgs->at(1)->expression;
            if (counter_type->is<IR::Constant>())
                value = counter_type->to<IR::Constant>()->asUnsigned();
            if (a->method->getName().name == "count") {
                auto args = a->expr->arguments;
                if (args->size() < 1){
                    ::error(ErrorType::ERR_UNEXPECTED, "Expected atleast 1 arguments for %1%",
                            a->object->getName());
                } else {
                    const IR::Expression *incr = nullptr;
                    auto index = args->at(0)->expression;
                    auto counter = a->object->getName();
                    if (args->size() == 2)
                        incr = args->at(1)->expression;
                    if (!incr && value > 0) {
                        ::error(ErrorType::ERR_UNEXPECTED,
                                "Expected packet length argument for %1%", a->object->getName());
                        return false;
                    }
                    if (value == 2) {
                        add_instr(new IR::DpdkCounterCountStatement(counter+"_packets",
                                                                        index));
                        add_instr(new IR::DpdkCounterCountStatement(counter+"_bytes",
                                                                        index, incr));
                    } else {
                         if (value == 1)
                             add_instr(new IR::DpdkCounterCountStatement(counter, index, incr));
                         else
                             add_instr(new IR::DpdkCounterCountStatement(counter, index));
                     }
                }
            } else {
                BUG("Counter function not implemented");
            }
        } else if (a->originalExternType->getName().name == "Register") {
            if (a->method->getName().name == "write") {
                auto args = a->expr->arguments;
                auto index = args->at(0)->expression;
                auto src = args->at(1)->expression;
                auto reg = a->object->getName();
                add_instr(new IR::DpdkRegisterWriteStatement(reg, index, src));
            }
        } else {
            ::error("%1%: Unknown extern function.", s);
        }
    } else if (auto a = mi->to<P4::ExternFunction>()) {
        LOG3("extern function: " << dbp(s) << std::endl << s);
        if (a->method->name == "verify") {
            if (parser == nullptr)
                ::error("%1%: verify must be used in parser", s);
            auto args = a->expr->arguments;
            auto condition = args->at(0);
            auto error_id = args->at(1);
            auto end_label = refmap->newName("label_end");
            const IR::BoolLiteral *boolLiteral = condition->expression->to<IR::BoolLiteral>();
            if (!boolLiteral) {
                add_instr(new IR::DpdkJmpNotEqualStatement(
                            end_label,
                            condition->expression, new IR::BoolLiteral(false)));
            } else if (boolLiteral->value == true) {
                return false;
            }
            IR::PathExpression *error_meta_path;
            if (structure->isPSA()) {
                error_meta_path = new IR::PathExpression(
                    IR::ID("m.psa_ingress_input_metadata_parser_error"));
            } else if (structure->isPNA()) {
                error_meta_path = new IR::PathExpression(
                    IR::ID("m.pna_pre_input_metadata_parser_error"));
            } else {
                BUG("Unknown architecture unexpectedly!");
            }
            add_instr(new IR::DpdkMovStatement(error_meta_path, error_id->expression));
            add_instr(new IR::DpdkJmpLabelStatement(
                        append_parser_name(parser, IR::ParserState::reject)));
            add_instr(new IR::DpdkLabelStatement(end_label));
        } else if (a->method->name == "add_entry") {
            auto action = a->expr->arguments->at(0)->expression;
            auto action_name = action->to<IR::StringLiteral>()->value;
            auto param = a->expr->arguments->at(1)->expression;
            if (param->is<IR::Member>()) {
                auto argument = param->to<IR::Member>();
                add_instr(new IR::DpdkLearnStatement(action_name, argument));
            } else if (param->is<IR::StructExpression>()) {
                auto argument = param->to<IR::StructExpression>()->components.at(0)->expression;
                add_instr(new IR::DpdkLearnStatement(action_name, argument));
            } else if (param->is<IR::Constant>()) {
                add_instr(new IR::DpdkLearnStatement(action_name, param));
            } else {
                ::error("%1%: unhandled function", s);
            }
        } else if (a->method->name == "send_to_port") {
            BUG_CHECK(a->expr->arguments->size() == 1,
                "%1%: expected one argument for send_to_port extern", a);
            add_instr(new IR::DpdkMovStatement(
                new IR::Member(new IR::PathExpression("m"), PnaMainOutputMetadataOutputPortName),
                a->expr->arguments->at(0)->expression));
        } else if (a->method->name == "drop_packet") {
            add_instr(new IR::DpdkDropStatement());
        } else {
            ::error("%1%: Unknown extern function", s);
        }
    } else if (auto a = mi->to<P4::BuiltInMethod>()) {
        LOG3("builtin method: " << dbp(s) << std::endl << s);
        if (a->name == "setValid") {
            add_instr(new IR::DpdkValidateStatement(a->appliedTo));
        } else if (a->name == "setInvalid") {
            add_instr(new IR::DpdkInvalidateStatement(a->appliedTo));
        } else {
            BUG("%1% function not implemented.", s);
        }
    } else if (auto a = mi->to<P4::ActionCall>()) {
        LOG3("action call: " << dbp(s) << std::endl << s);
        auto helper = new DPDK::ConvertStatementToDpdk(refmap, typemap, structure);
        helper->setCalledBy(this);
        a->action->body->apply(*helper);
        for (auto i : helper->get_instr()) {
            add_instr(i);
        }
    } else {
        BUG("%1% function not implemented.", s);
    }
    return false;
}

// This function assumes EliminateSwitch midend pass is applied and only handles
// action_run in switch expression.
bool ConvertStatementToDpdk::preorder(const IR::SwitchStatement *s) {
    auto tc = P4::TableApplySolver::isActionRun(s->expression, refmap, typemap);
    BUG_CHECK(tc != nullptr, "%1%: unexpected switch statement expression",
              s->expression);
    auto size = s->cases.size();
    std::vector <cstring> labels;
    cstring label;
    cstring default_label = "";
    auto end_label = refmap->newName("label_endswitch");
    // Emit jmp on action run statements and collect labels for each case statement
    for (unsigned i = 0; i < size - 1; i++) {
        auto caseLabel = s->cases.at(i);
        auto pe = caseLabel->label->to<IR::PathExpression>();
        CHECK_NULL(pe);
        label = refmap->newName("label_action");
        add_instr(new IR::DpdkJmpIfActionRunStatement(label, pe->path->name.name));
        labels.push_back(label);
    }

    if (s->cases.at(size-1)->label->is<IR::DefaultExpression>()) {
        label = refmap->newName("label_default");
        add_instr(new IR::DpdkJmpLabelStatement(label));
        default_label = label;
    } else {
        auto pe = s->cases.at(size-1)->label->to<IR::PathExpression>();
        CHECK_NULL(pe);
        label = refmap->newName("label_action");
        add_instr(new IR::DpdkJmpIfActionRunStatement(label, pe->path->name.name));
        add_instr(new IR::DpdkJmpLabelStatement(end_label));
    }

    labels.push_back(label);

    // Emit block statements corresponding to each case
    // We emit labels even for the fallthrough case, DpdkAsmOptimization pass will
    // remove any redundant jumps and labels.
    for (unsigned i = 0; i < size; i++) {
        auto caseLabel = s->cases.at(i);
        label = labels.at(i);
        add_instr(new IR::DpdkLabelStatement(label));
        if (caseLabel->statement != nullptr) {
            visit(caseLabel->statement);
            if (label != default_label)
                add_instr(new IR::DpdkJmpLabelStatement(end_label));
        }
    }
    add_instr(new IR::DpdkLabelStatement(end_label));
    return false;
}

}  // namespace DPDK
