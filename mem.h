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


typedef uint16_t dmem_t;

///Data memory for ANEM
class ANEMDataMemory
{
private:
	dmem_t dmem[];
	uint32_t size;
public:
	ANEMDataMemory(uint32_t size);
	uint16_t read(uint32_t address);
	void write(uint32_t address, dmem_t data);

	void clearMem(void);

};

///Instruction memory for ANEM
class ANEMInstructionMemory
{
private:
	ANEMInstruction imem[];
	uint32_t size;

public:
	ANEMInstructionMemory(uint32_t size);
	uint16_t fetch(uint32_t address);

};



#endif /* MEM_H_ */
