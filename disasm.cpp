/*
 * @file disasm.cpp
 * @brief ANEM16 instruction disassembler
 */

#include "disasm.h"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <unordered_map>

static std::unordered_map<addr_t, std::string> g_symbols;

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
    case ANEM_M1FUNC_SYSCALL: return "SYSCALL";
    case ANEM_M1FUNC_LPM: return "LPM";
    case ANEM_M1FUNC_M4: return "M4";
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
        case ANEM_M1FUNC_LPM:
            return "LPM " + regName(i.func) + ", " + rb;
        case ANEM_M1FUNC_SYSCALL:
            return "SYSCALL " + std::to_string(i.byte);
        case ANEM_M1FUNC_M4:
            switch (i.regb) {
            case ANEM_M4SUB_RETI:  return "RETI";
            case ANEM_M4SUB_EI:    return "EI";
            case ANEM_M4SUB_DI:    return "DI";
            case ANEM_M4SUB_MFEPC: return "MFEPC " + regName(i.func);
            case ANEM_M4SUB_MFECA: return "MFECA " + regName(i.func);
            case ANEM_M4SUB_MTEPC: return "MTEPC " + regName(i.func);
            default: return "M4? " + hexStr(i.byte);
            }
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

void loadSymbols(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return;

    g_symbols.clear();
    std::string name;
    addr_t addr;
    while (f >> name >> addr)
        g_symbols[addr] = name;
}

void addSymbol(addr_t addr, const std::string& name)
{
    g_symbols[addr] = name;
}

void clearSymbols()
{
    g_symbols.clear();
}

std::string lookupSymbol(addr_t addr)
{
    auto it = g_symbols.find(addr);
    if (it != g_symbols.end())
        return it->second;
    return {};
}

// Source location map
static std::unordered_map<addr_t, SourceLoc> g_srcLocs;

void addSourceLoc(addr_t addr, const std::string& file, int line)
{
    g_srcLocs[addr] = {file, line};
}

SourceLoc lookupSourceLoc(addr_t addr)
{
    auto it = g_srcLocs.find(addr);
    if (it != g_srcLocs.end())
        return it->second;
    return {};
}

void clearSourceLocs()
{
    g_srcLocs.clear();
}

// Variable mapping
static std::vector<VarMapping> g_varmap;

void loadVarmap(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return;

    g_varmap.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ss(line);
        VarMapping v;
        std::string startS, endS;
        if (!(ss >> startS >> endS >> v.name >> v.location))
            continue;
        v.startPC = (addr_t)std::stoul(startS, nullptr, 16);
        v.endPC = (addr_t)std::stoul(endS, nullptr, 16);
        g_varmap.push_back(std::move(v));
    }
}

void addVarMapping(addr_t startPC, addr_t endPC, const std::string& name, const std::string& loc)
{
    g_varmap.push_back({startPC, endPC, name, loc});
}

void clearVarmap()
{
    g_varmap.clear();
}

std::vector<VarMapping> lookupVarsAtPC(addr_t pc)
{
    std::vector<VarMapping> result;
    for (const auto& v : g_varmap) {
        if (pc >= v.startPC && pc < v.endPC)
            result.push_back(v);
    }
    return result;
}

static int16_t signExt12(uint16_t val)
{
    if (val & 0x800)
        return (int16_t)(val | 0xF000);
    return (int16_t)val;
}

std::string disassemble(const ANEMInstruction& i, addr_t pc)
{
    if (g_symbols.empty())
        return disassemble(i);

    // For branch/jump opcodes, try to resolve the target to a symbol
    addr_t target;
    std::string sym;
    std::string mnemonic;

    switch (i.opcode) {
    case ANEM_OPCODE_J:
    case ANEM_OPCODE_JAL:
        target = (addr_t)((int32_t)pc + 1 + signExt12(i.address));
        sym = lookupSymbol(target);
        if (!sym.empty()) {
            mnemonic = (i.opcode == ANEM_OPCODE_J) ? "J" : "JAL";
            return mnemonic + " " + sym;
        }
        break;

    case ANEM_OPCODE_BZ:
    case ANEM_OPCODE_BZ_T:
    case ANEM_OPCODE_BZ_N:
    case ANEM_OPCODE_BHLEQ:
        target = (addr_t)((int32_t)pc + 2 + signExt12(i.address));
        sym = lookupSymbol(target);
        if (!sym.empty()) {
            if (i.opcode == ANEM_OPCODE_BZ)        mnemonic = "BZ";
            else if (i.opcode == ANEM_OPCODE_BZ_T)  mnemonic = "BZ.T";
            else if (i.opcode == ANEM_OPCODE_BZ_N)  mnemonic = "BZ.N";
            else                                     mnemonic = "BHLEQ";
            return mnemonic + " " + sym;
        }
        break;

    default:
        break;
    }

    // No symbol match — fall through to base disassembly
    return disassemble(i);
}
