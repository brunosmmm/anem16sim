/*
 * @file disasm.h
 * @brief ANEM16 instruction disassembler
 */

#ifndef DISASM_H_
#define DISASM_H_

#include "instrset.h"
#include <string>
#include <cstdint>

std::string disassemble(const ANEMInstruction& i);
std::string regName(uint8_t r);
std::string opcodeName(uint8_t opcode);

#endif /* DISASM_H_ */
