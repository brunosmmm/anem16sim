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

struct f2d
{
	ANEMInstruction ireg;

	bool bubble;
};

//decode to execute "registers"
struct d2e
{
	ANEMRegBnkOp reg_ctl;
	ANEMAluOp alu_ctl;
	ANEMAluFunc alu_func;
	uint8_t alu_shamt;

	uint8_t rega_sel;
	uint8_t regb_sel;

	data_t rega_out;
	data_t regb_out;

	//memory access
	bool mem_enable;
	bool mem_write;

	//immediate values
	uint8_t imm_val;

	//offset for memory access
	uint8_t off_4;

	bool bubble;
};

//execute to memory "registers"
struct e2m
{
	ANEMRegBnkOp reg_ctl;
	ANEMAluOut  alu_out;

	uint8_t rega_sel;
	data_t rega_out;

	//memory
	bool mem_enable;
	bool mem_write;

	//immediate
	uint8_t imm_val;

	bool bubble;
};

//memory to writeback "registers"
struct m2w
{
	ANEMRegBnkOp reg_ctl;
	ANEMAluOut alu_out;
	data_t mem_out;

	uint8_t rega_sel;

	//immediate
	uint8_t imm_val;

	bool bubble;
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
	ANEMInstruction p_fetch(void);
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

	//pipeline registers
	struct f2d fetch_to_decode;
	struct d2e decode_to_exec;
	struct e2m exec_to_mem;
	struct m2w mem_to_wb;
public:
	ANEMCPU(bool fw_enable);
	void reset(void);
	void clockCycle(void);
	bool programEnd(void);

	void loadProgram(std::string fileName);

};



#endif /* CPU_H_ */
