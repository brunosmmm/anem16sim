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

	//jumps
	bool j_flag; //J and JAL types
	bool jr_flag;
	bool bz_flag;

	addr_t j_dest; //for J, JR and JAL

	bool fwd_alu_alua;
	bool fwd_alu_alub;
	bool fwd_mem_alua;
	bool fwd_mem_alub;

	data_t fwd_alua;
	data_t fwd_alub;

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
	data_t rega_out; ///THIS MUST BE PRESENT IN ORDER TO FORWARD AN IMMEDIATE VALUE
	///@todo modify this in anem16pipe

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
	bool p_stall_id = false;
	bool p_stall_ex = false;
	bool p_stall_mem = false;
	bool p_stall_wb = false;
	bool p_stall_master = false;
	unsigned int stallCounter = 0;

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

	//helper functions
	data_t getFwdValFromEX(void);
	data_t getFwdValFromMEM(void);
	void insertStalls(unsigned int stallCount) { this->p_stall_if = true; this->stallCounter = stallCount; }
	void manageStalls(void);
public:
	ANEMCPU(bool fw_enable);
	void reset(void);
	void clockCycle(void);
	bool programEnd(void);

	void loadProgram(std::string fileName);
	void attachPeripheral(addr_t addr, ANEMMemMappedPeripheral p) { this->dmem.attachPeripheral(addr,p); }

};



#endif /* CPU_H_ */
