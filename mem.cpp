/*
 * @file mem.cpp
 * @brief ANEM data and instruction memory emulation
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "mem.h"

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
	this->dmem = new uint16_t[size];

	memset((void*)this->dmem,0x00,sizeof(dmem_t)*size);

}
