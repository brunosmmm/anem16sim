/*
 * cpu.h
 *
 *  Created on: 05/12/2014
 *      Author: bruno
 */

#ifndef CPU_H_
#define CPU_H_

#include "instrset.h"
#include "mem.h"

class ANEMCPU
{
private:
	uint16_t regbnk[16];
	uint32_t pc;
	ANEMDataMemory dmem();
	ANEMInstructionMemory imem();
public:
	void reset(void);
	void clockCycle(void);

};



#endif /* CPU_H_ */
