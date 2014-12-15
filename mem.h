/****
 * @file mem.h
 * @brief instruction and data memory
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#ifndef MEM_H_
#define MEM_H_

#include <cstdint>
#include "instrset.h"
#include <fstream>

typedef uint16_t dmem_t;
typedef uint16_t data_t;
typedef uint32_t addr_t;

///Data memory for ANEM
class ANEMDataMemory
{
private:
	dmem_t * dmem;
	uint32_t size;
public:
	ANEMDataMemory() {dmem = nullptr; size = 0;};
	ANEMDataMemory(uint32_t size);
	uint16_t read(addr_t address);
	void write(addr_t address, dmem_t data);

	void clearMem(void);

};

///Instruction memory for ANEM
class ANEMInstructionMemory
{
private:
	ANEMInstruction * imem;
	uint32_t size;

public:
	ANEMInstructionMemory() {imem = nullptr, size=0; };
	ANEMInstructionMemory(uint32_t size);
	ANEMInstruction fetch(addr_t address);

	//load instructions from file
	void loadProgram(std::string fileName);

};



#endif /* MEM_H_ */
