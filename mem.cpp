/*
 * @file mem.cpp
 * @brief ANEM data and instruction memory emulation
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "mem.h"
#include <cstring>

/***
 * @brief Allocates memory for the instruction mem
 * @param size Memory size in words
 */
ANEMInstructionMemory::ANEMInstructionMemory(uint32_t size)
{

	this->size = size;
	///allocate
	this->imem = new ANEMInstruction[size];

	///initialize to zeros
	memset((void*)this->imem,0x00,sizeof(ANEMInstruction)*size);

}

ANEMDataMemory::ANEMDataMemory(uint32_t size)
{
	this->size = size;

	//allocate
	this->dmem = new dmem_t[size];

	//clear memory
	this->clearMem();

}

void ANEMDataMemory::clearMem(void)
{
	//set all to zeros
	memset((void*)this->dmem,0x0000,sizeof(dmem_t)*this->size);

}

data_t ANEMDataMemory::read(addr_t address)
{

	if (address > this->size)
	{
		//exception
		return 0xFFFF;
	}

	return this->dmem[address];

}

void ANEMDataMemory::write(addr_t address, dmem_t data)
{

	if (address > this->size)
	{

		//exception
		return;
	}

	this->dmem[address] = data;

}

ANEMInstruction ANEMInstructionMemory::fetch(addr_t addr)
{

	ANEMInstruction nop = {0};

	if (addr > this->size)
	{
		//exception, do something here
		return nop;
	}

	return this->imem[addr];

}
