/****
 * @file cpu.cpp
 * @brief ANEM CPU
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"

void ANEMCPU::reset(void)
{

	//clear data memory
	this->dmem.clearMem();
	//clear registers
	//zero PC


}


void ANEMCPU::clockCycle(void)
{

	//fetch

	//decode

	//execute (ALU)

	//memory

	//writeback

}

