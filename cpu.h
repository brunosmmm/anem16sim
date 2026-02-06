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
#include "stats/stats.h"
#include <functional>

#define GPR_COUNT 16

enum ANEMHILOOp {loadUpper, loadLower, fromRegister, doAIS, doAIH_AIL, noOp};

struct f2d
{
	ANEMInstruction ireg;

	bool bubble;
	addr_t savedpc;
	addr_t pc;  // PC of this instruction
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
	bool bhleq_flag;

	addr_t j_dest; //for J, JR and JAL
	uint16_t bz_offset; // raw 12-bit branch offset (resolved in fetch, not decode)

	bool fwd_alu_alua;
	bool fwd_alu_alub;
	bool fwd_mem_alua;
	bool fwd_mem_alub;

	data_t fwd_alua;
	data_t fwd_alub;

	bool bubble;

	//special register logic
	ANEMHILOOp hictl;
	ANEMHILOOp loctl;
	data_t hiout;
	data_t loout;

	//saved pc for jal
	addr_t savedpc;

	addr_t pc;  // PC of this instruction
	ANEMInstruction ireg;  // original instruction (for disassembly)
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

	//special registers
	ANEMHILOOp hictl;
	ANEMHILOOp loctl;
	data_t hiout;
	data_t loout;

	addr_t savedpc;

	addr_t pc;  // PC of this instruction
	ANEMInstruction ireg;  // original instruction (for disassembly)
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

	ANEMHILOOp hictl;
	ANEMHILOOp loctl;
	data_t hiout;
	data_t loout;

	addr_t savedpc;

	addr_t pc;  // PC of this instruction
	ANEMInstruction ireg;  // original instruction (for disassembly)
};

// Trace callback type
class ANEMCPU;
using TraceCallback = std::function<void(unsigned long long cycle, addr_t pc,
                                         const ANEMInstruction& instr, const ANEMCPU& cpu)>;

class ANEMCPU
{
private:
	//structural units
	ANEMRegBnk regbnk;
	addr_t pc = 0x00000000;
	ANEMDataMemory dmem;
	ANEMInstructionMemory imem;
	ANEMAlu alu;

	//special registers
	data_t reghi;
	data_t reglo;

	//pipeline stages
        struct f2d p_fetch(void);
	struct d2e p_decode(struct f2d i);
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

	//simulation specifics
	bool fw_enable;
	unsigned int maxCycles = 10000;

	//pipeline registers
	struct f2d fetch_to_decode;
	struct d2e decode_to_exec;
	struct e2m exec_to_mem;
	struct m2w mem_to_wb;

	//counters
	ANEMCounters counters;

	// Trace
	TraceCallback traceCallback;

	// Cycle counter for programEnd (non-static)
	unsigned long long totalCycles = 0;
	addr_t lastPC = 0;
	unsigned int samePCCount = 0;

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
	void attachPeripheral(addr_t addr, ANEMMemMappedPeripheral *p) { this->dmem.attachPeripheral(addr,p); }
	void setMaxCycles(unsigned int n) { this->maxCycles = n; }

	// Diagnostic output
	void dumpRegisters(void);
	void dumpMemory(addr_t start, addr_t count);

	// State access
	addr_t getPC() const { return pc; }
	data_t getHI() const { return reghi; }
	data_t getLO() const { return reglo; }
	const struct f2d& getFetchToDecode() const { return fetch_to_decode; }
	const struct d2e& getDecodeToExec() const { return decode_to_exec; }
	const struct e2m& getExecToMem() const { return exec_to_mem; }
	const struct m2w& getMemToWB() const { return mem_to_wb; }
	bool isStalled() const { return p_stall_if; }
	unsigned long long getCycleCount() const { return totalCycles; }

	// Register/memory access for debugger
	data_t readRegister(uint8_t reg) const { return const_cast<ANEMRegBnk&>(regbnk).r_read(reg); }
	data_t readDataMem(addr_t addr) const { return const_cast<ANEMDataMemory&>(dmem).read(addr); }
	ANEMInstruction readInstrMem(addr_t addr) const { return const_cast<ANEMInstructionMemory&>(imem).fetch(addr); }

	// Trace support
	void setTraceCallback(TraceCallback cb) { traceCallback = cb; }

	// Statistics
	void dumpStats(std::ostream& out) const;
	const ANEMCounters& getCounters() const { return counters; }

	// Memory access logging
	ANEMDataMemory& getDataMemory() { return dmem; }
};



#endif /* CPU_H_ */
