
/*
 * 
 * pipeline.c
 * 
   This is the primary place for student's code.

 */


#include <stdlib.h>
#include <string.h>
#include "fu.h"
#include "pipeline.h"

#define INT_CYCLES 3

void writeback(state_t *state, int *num_insn) {
    if (state->int_wb.instr != 0) {
        int instr = state->int_wb.instr;
        int use_imm;
        const op_info_t *op_info = decode_instr(instr, &use_imm);

        switch(op_info->operation) {
            case OPERATION_ADD:
                int rd = FIELD_RD(instr);
                int rs1 = FIELD_RS1(instr);
                int rs2 = FIELD_RS2(instr);
                if (use_imm) {
                    state->rf_int.reg_int[rd].w = state->rf_int.reg_int[rs1].w + FIELD_IMM_I(instr);
                } else {
                    state->rf_int.reg_int[rd].w = state->rf_int.reg_int[rs1].w + state->rf_int.reg_int[rs2].w;
                }
                state->fetch_lock = FALSE;
                break;

            case OPERATION_SUB:
                rd = FIELD_RD(instr);
                rs1 = FIELD_RS1(instr);
                rs2 = FIELD_RS2(instr);
                state->rf_int.reg_int[rd].w = state->rf_int.reg_int[rs1].w - state->rf_int.reg_int[rs2].w;
                state->fetch_lock = FALSE;
                break;

            case OPERATION_LOAD: {
                int rd = FIELD_RD(instr);
                int rs1 = FIELD_RS1(instr);
                int imm_i = FIELD_IMM_I(instr);
                if (op_info->data_type == DATA_TYPE_W) {
                    state->rf_int.reg_int[rd].w = *(int *)&state->mem[state->rf_int.reg_int[rs1].w + imm_i];
                    state->fetch_lock = FALSE;
                }
                break;
            }

            case OPERATION_STORE: {
                int rs1 = FIELD_RS1(instr);
                int rs2 = FIELD_RS2(instr);
                int imm_s = FIELD_IMM_S(instr);
                if (op_info->data_type == DATA_TYPE_W) {
                    *(int *)&state->mem[state->rf_int.reg_int[rs1].w + imm_s] = state->rf_int.reg_int[rs2].w;
                }
                break;
            }

        }
        
        (*num_insn)++;
        state->int_wb.instr = 0;
    }

    if (state->fp_wb.instr != 0) {
        (*num_insn)++;
        state->fp_wb.instr = 0;
    }

}


void
execute(state_t *state) {
    advance_fu_int(state->fu_int_list, &state->int_wb);
    advance_fu_fp(state->fu_add_list, &state->fp_wb);
    advance_fu_fp(state->fu_mult_list, &state->fp_wb);
    advance_fu_fp(state->fu_div_list, &state->fp_wb);

    for (int i = 0; i < NUMREGS; i++) {
        if (state->rf_int.busy[i] > 0) {
            state->rf_int.busy[i]--;
        }
        if (state->rf_fp.busy[i] > 0) {
            state->rf_int.busy[i]--;
        }
    }
}


int
decode(state_t *state) {
    int instr = state->if_id.instr;
    if (instr == NOP) return 0;

    int use_imm;
    const op_info_t *op_info = decode_instr(instr, &use_imm);
    int rd, rs1, rs2, imm_i, imm_s, offset;

    switch (op_info->fu_group_num) {
        case FU_GROUP_NONE: break;
        case FU_GROUP_HALT:
            state->fetch_lock = TRUE;
            state->halt = TRUE;
            break;
        case FU_GROUP_INT: {
            int rd = FIELD_RD(instr);
            int rs1 = FIELD_RS1(instr);
            operand_t op1, op2;

            if (state->rf_int.busy[rs1] != 0) {
                state->fetch_lock = TRUE;
                return 1;
            }

            if (!use_imm) {
                int rs2 = FIELD_RS2(instr);
                if (state->rf_int.busy[rs2] != 0) {
                    state->fetch_lock = TRUE;
                    return 1;
                }
            }

            if (state->rf_int.busy[rd] >= INT_CYCLES + 1) {
                state->fetch_lock = TRUE;
                return 1;
            }

            if (issue_fu_int(state->fu_int_list, instr, state->if_id.pc) == -1) {
                state->fetch_lock = TRUE;
                return 1;
            }

            if (rd != 0) {
                state->rf_int.busy[rd] = INT_CYCLES;
            }
            break;
        }

        case FU_GROUP_MEM: {
            int rs1 = FIELD_RS1(instr);
            operand_t base_addr;
            
            if (state->rf_int.busy[rs1] != 0) {
                state->fetch_lock = TRUE;
                return 1;
            }
            
            base_addr.integer = state->rf_int.reg_int[rs1];

            if (op_info->operation == OPERATION_LOAD) {
                rd = FIELD_RD(instr);
                if (op_info->data_type == DATA_TYPE_W) {
                    if (state->rf_int.busy[rd] != 0) {
                        state->fetch_lock = TRUE;
                        return 1;
                    }
                } else if (op_info->data_type == DATA_TYPE_F) {
                    if (state->rf_fp.busy[rd] != 0) {
                        state->fetch_lock = TRUE;
                        return 1;
                    }
                }
            } else {
                rs2 = FIELD_RS2(instr);
                if (op_info->data_type == DATA_TYPE_W) {
                    if (state->rf_int.busy[rs2] != 0) {
                        state->fetch_lock = TRUE;
                        return 1;
                    }
                } else if (op_info->data_type == DATA_TYPE_F) {
                    if (state->rf_fp.busy[rs2] != 0) {
                        state->fetch_lock = TRUE;
                        return 1;
                    }
                }
            }

            if (issue_fu_int(state->fu_int_list, instr, state->if_id.pc) == -1) {
                state->fetch_lock = TRUE;
                return 1;
            }

            if (op_info->operation == OPERATION_LOAD) {
                rd = FIELD_RD(instr);
                if (op_info->data_type == DATA_TYPE_W) {
                    if (rd != 0) { 
                        state->rf_int.busy[rd] = INT_CYCLES;
                    }
                } else if (op_info->data_type == DATA_TYPE_F) {
                    state->rf_fp.busy[rd] = INT_CYCLES;
                }
            }
            break;
        }

        case FU_GROUP_ADD:
        if (issue_fu_fp(state->fu_add_list, instr) == -1) {
            return 1;
        }
        break;

        case FU_GROUP_MULT:
        if (issue_fu_fp(state->fu_mult_list, instr) == -1) {
            return 1;
        }
        break;

        case FU_GROUP_DIV:
        if (issue_fu_fp(state->fu_div_list, instr) == -1) {
            return 1;
        }
        break;

        case FU_GROUP_BRANCH:
            if (issue_fu_int(state->fu_int_list, instr, state->if_id.pc) == -1) {
                return 1;
            }
            break;
        case FU_GROUP_INVALID:
            fprintf(stderr, "error: invalid instruction group\n");
            return -1;
            break;
        default:
            fprintf(stderr, "error: unknown instruction group\n");
            return -1;
            break;
    }
    return 0;
}

void
fetch(state_t *state) {
    if (!state->fetch_lock || state->halt) {
        state->if_id.instr = *(int *)&state->mem[state->pc];
        state->if_id.pc = state->pc;
        state->pc += 4;
    }
}