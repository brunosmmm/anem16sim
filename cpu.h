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
#include "regbnk.h"
#include "alu.h"

#define GPR_COUNT 16

//decode to execute "registers"
struct d2e
{
	ANEMRegBnkOp reg_ctl;
	ANEMAluOp alu_ctl;

};

//execute to memory "registers"
struct e2m
{
	ANEMRegBnkOp reg_ctl;

};

//memory to writeback "registers"
struct m2w
{
	ANEMRegBnkOp reg_ctl;

};

class ANEMCPU
{
private:
	//structural units
	ANEMRegBnk regbnk;
	addr_t pc = 0x00000000;
	ANEMDataMemory dmem;
	ANEMInstructionMemory imem;
	ANEMAlu alu;

	//pipeline stages
	ANEMInstruction p_fetch(addr_t addr);
	struct d2e p_decode(ANEMInstruction i);
	struct e2m p_execute(struct d2e d);
	struct m2w p_mem(struct e2m e);
	void p_writeback(struct m2w m);

	//stall control
	bool p_stall_if = false;

	//forwarding flags
	bool fwd_alu_alua = false;
	bool fwd_alu_alub = false;
	bool fwd_mem_alua = false;
	bool fwd_mem_alub = false;

	//hazards


	//simulation specifics
	bool fw_enable;
public:
	ANEMCPU(bool fw_enable);
	void reset(void);
	void clockCycle(void);
	bool programEnd(void);

};



#endif /* CPU_H_ */
