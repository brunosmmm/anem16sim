/*
 * @file disasm.h
 * @brief ANEM16 instruction disassembler
 */

#ifndef DISASM_H_
#define DISASM_H_

#include "instrset.h"
#include "types.h"
#include <string>
#include <vector>
#include <cstdint>

std::string disassemble(const ANEMInstruction& i);
std::string disassemble(const ANEMInstruction& i, addr_t pc);
void loadSymbols(const std::string& path);
void addSymbol(addr_t addr, const std::string& name);
void clearSymbols();
std::string lookupSymbol(addr_t addr);
std::string regName(uint8_t r);
std::string opcodeName(uint8_t opcode);

// Source location mapping (from DWARF .debug_line)
struct SourceLoc {
	std::string file;
	int line;
};
void addSourceLoc(addr_t addr, const std::string& file, int line);
SourceLoc lookupSourceLoc(addr_t addr);
void clearSourceLocs();

// Variable-to-register/stack mapping (from .varmap files)
struct VarMapping {
	addr_t startPC;
	addr_t endPC;
	std::string name;
	std::string location;  // "R3", "stack+4", etc.
};
void loadVarmap(const std::string& path);
void addVarMapping(addr_t startPC, addr_t endPC, const std::string& name, const std::string& loc);
void clearVarmap();
// Returns all variable mappings active at the given PC
std::vector<VarMapping> lookupVarsAtPC(addr_t pc);

#endif /* DISASM_H_ */
