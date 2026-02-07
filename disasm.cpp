/*
 * @file disasm.cpp
 * @brief ANEM16 instruction disassembler
 */

#include "disasm.h"
#include <sstream>
#include <iomanip>

std::string regName(uint8_t r)
{
    return "R" + std::to_string(r & 0xF);
}

static std::string hexStr(uint16_t val, int width = 2)
{
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << val;
    return ss.str();
}

static bool isNop(const ANEMInstruction& i)
{
    // NOP = R0 OR R0 → opcode R, rega=0, regb=0, func=OR
    return i.opcode == ANEM_OPCODE_R && i.rega == 0 && i.regb == 0 && i.func == ANEM_FUNC_OR;
}

static std::string rFuncName(uint8_t func)
{
    switch (func) {
    case ANEM_FUNC_ADD: return "ADD";
    case ANEM_FUNC_SUB: return "SUB";
    case ANEM_FUNC_OR:  return "OR";
    case ANEM_FUNC_AND: return "AND";
    case ANEM_FUNC_XOR: return "XOR";
    case ANEM_FUNC_NOR: return "NOR";
    case ANEM_FUNC_SLT: return "SLT";
    case ANEM_FUNC_SGT: return "SGT";
    case ANEM_FUNC_MUL: return "MUL";
    default: return "R?";
    }
}

static std::string sFuncName(uint8_t func)
{
    switch (func) {
    case ANEM_FUNC_SHL: return "SHL";
    case ANEM_FUNC_SHR: return "SHR";
    case ANEM_FUNC_SAR: return "SAR";
    case ANEM_FUNC_ROL: return "ROL";
    case ANEM_FUNC_ROR: return "ROR";
    default: return "S?";
    }
}

static std::string m1FuncName(uint8_t func)
{
    switch (func) {
    case ANEM_M1FUNC_LHL:  return "LHL";
    case ANEM_M1FUNC_LHH:  return "LHH";
    case ANEM_M1FUNC_LLL:  return "LLL";
    case ANEM_M1FUNC_LLH:  return "LLH";
    case ANEM_M1FUNC_AIS:  return "AIS";
    case ANEM_M1FUNC_AIH:  return "AIH";
    case ANEM_M1FUNC_AIL:  return "AIL";
    case ANEM_M1FUNC_MFHI: return "MFHI";
    case ANEM_M1FUNC_MFLO: return "MFLO";
    case ANEM_M1FUNC_MTHI: return "MTHI";
    case ANEM_M1FUNC_MTLO: return "MTLO";
    default: return "M1?";
    }
}

std::string opcodeName(uint8_t opcode)
{
    switch (opcode) {
    case ANEM_OPCODE_R:     return "R-type";
    case ANEM_OPCODE_S:     return "S-type";
    case ANEM_OPCODE_SW:    return "SW";
    case ANEM_OPCODE_LW:    return "LW";
    case ANEM_OPCODE_LIU:   return "LIU";
    case ANEM_OPCODE_LIL:   return "LIL";
    case ANEM_OPCODE_BZ:    return "BZ";
    case ANEM_OPCODE_BZ_T:  return "BZ.T";
    case ANEM_OPCODE_BZ_N:  return "BZ.N";
    case ANEM_OPCODE_JR:    return "JR";
    case ANEM_OPCODE_JAL:   return "JAL";
    case ANEM_OPCODE_J:     return "J";
    case ANEM_OPCODE_M1:    return "M1";
    case ANEM_OPCODE_BHLEQ: return "BHLEQ";
    case ANEM_OPCODE_STACK: return "STACK";
    case ANEM_OPCODE_ADDI:  return "ADDI";
    default: return "???";
    }
}

std::string disassemble(const ANEMInstruction& i)
{
    // Reconstruct the instruction word for unknown display
    uint16_t iword = ((uint16_t)i.opcode << 12) | ((uint16_t)i.rega << 8) | i.byte;

    // Halt detection
    if (iword == 0xFFFF)
        return "HALT";

    // NOP detection
    if (isNop(i))
        return "NOP";

    std::string ra = regName(i.rega);
    std::string rb = regName(i.regb);

    switch (i.opcode) {
    case ANEM_OPCODE_R:
        return rFuncName(i.func) + " " + ra + ", " + ra + ", " + rb;

    case ANEM_OPCODE_S:
        return sFuncName(i.func) + " " + ra + ", " + ra + ", #" + std::to_string(i.shamt);

    case ANEM_OPCODE_LW:
        return "LW " + ra + ", " + rb + "(" + std::to_string(i.off_4) + ")";

    case ANEM_OPCODE_SW:
        return "SW " + ra + ", " + rb + "(" + std::to_string(i.off_4) + ")";

    case ANEM_OPCODE_LIU:
        return "LIU " + ra + ", " + hexStr(i.byte);

    case ANEM_OPCODE_LIL:
        return "LIL " + ra + ", " + hexStr(i.byte);

    case ANEM_OPCODE_J:
        return "J " + hexStr(i.address, 3);

    case ANEM_OPCODE_JAL:
        return "JAL " + hexStr(i.address, 3);

    case ANEM_OPCODE_JR:
        return "JR " + ra;

    case ANEM_OPCODE_BZ:
        return "BZ " + hexStr(i.address, 3);

    case ANEM_OPCODE_BZ_T:
        return "BZ.T " + hexStr(i.address, 3);

    case ANEM_OPCODE_BZ_N:
        return "BZ.N " + hexStr(i.address, 3);

    case ANEM_OPCODE_BHLEQ:
        return "BHLEQ " + hexStr(i.address, 3);

    case ANEM_OPCODE_M1: {
        // M1 sub-function is in bits[11:8] (rega field), not bits[3:0]
        std::string fn = m1FuncName(i.rega);
        // MFHI/MFLO/MTHI/MTLO: register is in bits[3:0] (func), not bits[11:8] (rega)
        std::string rd = regName(i.func);
        switch (i.rega) {
        case ANEM_M1FUNC_MFHI:
        case ANEM_M1FUNC_MFLO:
            return fn + " " + rd;
        case ANEM_M1FUNC_MTHI:
        case ANEM_M1FUNC_MTLO:
            return fn + " " + rd;
        case ANEM_M1FUNC_LHL:
        case ANEM_M1FUNC_LHH:
        case ANEM_M1FUNC_LLL:
        case ANEM_M1FUNC_LLH:
        case ANEM_M1FUNC_AIH:
        case ANEM_M1FUNC_AIL:
        case ANEM_M1FUNC_AIS:
            return fn + " " + hexStr(i.byte);
        default:
            return fn;
        }
    }

    case ANEM_OPCODE_STACK: {
        switch (i.func) {
        case ANEM_STACKFUNC_PUSH: return "PUSH " + ra;
        case ANEM_STACKFUNC_POP:  return "POP " + ra;
        case ANEM_STACKFUNC_SPRD: return "SPRD " + ra;
        case ANEM_STACKFUNC_SPWR: return "SPWR " + ra;
        default: return "STACK? " + hexStr(iword, 4);
        }
    }

    case ANEM_OPCODE_ADDI: {
        int8_t simm = (int8_t)i.byte;
        return "ADDI " + ra + ", " + std::to_string(simm);
    }

    default:
        return "??? (" + hexStr(iword, 4) + ")";
    }
}
