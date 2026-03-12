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
#include "trace.h"
#include <functional>

#define GPR_COUNT 16

enum ANEMHILOOp {loadUpper, loadLower, fromRegister, doAIS, doAIH_AIL, fromMultiply, noOp};
enum ANEMSPCtl { spNone = 0, spPush, spPop, spRead, spWrite };

struct f2d
{
	ANEMInstruction ireg{};

	bool bubble = false;
	addr_t savedpc = 0;
	addr_t pc = 0;  // PC of this instruction
};

//decode to execute "registers"
struct d2e
{
	ANEMRegBnkOp reg_ctl = regNOP;
	ANEMAluOp alu_ctl = aluNOP;
	ANEMAluFunc alu_func = aluADD;
	uint8_t alu_shamt = 0;

	uint8_t rega_sel = 0;
	uint8_t regb_sel = 0;

	data_t rega_out = 0;
	data_t regb_out = 0;

	//memory access
	bool mem_enable = false;
	bool mem_write = false;

	//immediate values
	uint8_t imm_val = 0;

	//offset for memory access
	uint8_t off_4 = 0;

	//jumps
	bool j_flag = false; //J and JAL types
	bool jr_flag = false;
	bool bz_flag = false;
	bool bz_negate = false; // true for BZ_N (branch when Z=0), false for BZ/BZ_T
	bool bhleq_flag = false;

	addr_t j_dest = 0; //for J, JR and JAL
	uint16_t bz_offset = 0; // raw 12-bit branch offset (resolved in fetch, not decode)

	bool fwd_alu_alua = false;
	bool fwd_alu_alub = false;
	bool fwd_mem_alua = false;
	bool fwd_mem_alub = false;

	data_t fwd_alua = 0;
	data_t fwd_alub = 0;

	bool bubble = false;

	//special register logic
	ANEMHILOOp hictl = noOp;
	ANEMHILOOp loctl = noOp;
	data_t hiout = 0;
	data_t loout = 0;

	//saved pc for jal
	addr_t savedpc = 0;

	//stack pointer
	ANEMSPCtl sp_ctl = spNone;
	data_t sp_val = 0;
	bool alu_imm_sel = false;

	// Exception control
	uint8_t exc_ctl = 0;  // 0=none, 1=MFEPC, 2=MFECA, 3=MTEPC
	bool syscall_flag = false;
	bool reti_flag = false;

	// LPM: load from instruction memory instead of data memory
	bool lpm_flag = false;

	addr_t pc = 0;  // PC of this instruction
	ANEMInstruction ireg{};  // original instruction (for disassembly)
};

//execute to memory "registers"
struct e2m
{
	ANEMRegBnkOp reg_ctl = regNOP;
	ANEMAluOut  alu_out{};

	uint8_t rega_sel = 0;
	data_t rega_out = 0;

	//memory
	bool mem_enable = false;
	bool mem_write = false;

	//immediate
	uint8_t imm_val = 0;

	bool bubble = false;

	//special registers
	ANEMHILOOp hictl = noOp;
	ANEMHILOOp loctl = noOp;
	data_t hiout = 0;
	data_t loout = 0;

	addr_t savedpc = 0;

	//stack pointer
	ANEMSPCtl sp_ctl = spNone;
	data_t sp_new = 0;
	data_t sp_addr = 0;

	// Exception control
	uint8_t exc_ctl = 0;  // 0=none, 1=MFEPC, 2=MFECA, 3=MTEPC

	// LPM: load from instruction memory instead of data memory
	bool lpm_flag = false;

	addr_t pc = 0;  // PC of this instruction
	ANEMInstruction ireg{};  // original instruction (for disassembly)
};

//memory to writeback "registers"
struct m2w
{
	ANEMRegBnkOp reg_ctl = regNOP;
	ANEMAluOut alu_out{};
	data_t mem_out = 0;

	uint8_t rega_sel = 0;
	data_t rega_out = 0; ///THIS MUST BE PRESENT IN ORDER TO FORWARD AN IMMEDIATE VALUE
	///@todo modify this in anem16pipe

	//immediate
	uint8_t imm_val = 0;

	bool bubble = false;

	ANEMHILOOp hictl = noOp;
	ANEMHILOOp loctl = noOp;
	data_t hiout = 0;
	data_t loout = 0;

	addr_t savedpc = 0;

	//stack pointer
	ANEMSPCtl sp_ctl = spNone;
	data_t sp_new = 0;

	// Exception control
	uint8_t exc_ctl = 0;  // 0=none, 1=MFEPC, 2=MFECA, 3=MTEPC

	addr_t pc = 0;  // PC of this instruction
	ANEMInstruction ireg{};  // original instruction (for disassembly)
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
	data_t reghi = 0;
	data_t reglo = 0;
	data_t regsp = 0;

	// Exception/interrupt registers
	data_t epc = 0;        // Exception PC register
	data_t eca = 0;        // Exception Cause register
	bool ien = false;       // Interrupt Enable flag
	bool int_pin = false;   // External interrupt input pin

	// Persistent Z flag register (hardware: p_alu_mem_z_2, gated by p_z_en).
	// Only updates on R-type or S-type ALU operations; non-ALU instructions
	// (LIL, LIU, LW, etc.) preserve the previous value.
	bool z_flag = false;

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
	struct f2d fetch_to_decode{};
	struct d2e decode_to_exec{};
	struct e2m exec_to_mem{};
	struct m2w mem_to_wb{};

	//counters
	ANEMCounters counters;

	// Trace
	TraceCallback traceCallback;
	TraceWriter* traceWriter = nullptr;

	// Cycle counter for programEnd (non-static)
	unsigned long long totalCycles = 0;
	addr_t lastPC = 0;
	addr_t prevPC = 0;
	unsigned int samePCCount = 0;
	int drainCycles = -1;  // pipeline drain: -1 = not draining, >=0 = countdown

	//helper functions
	data_t getFwdValFromEX(void);
	data_t getFwdValFromMEM(void);
	data_t getForwardedSP(void) const;
	void insertStalls(unsigned int stallCount) { this->p_stall_if = true; this->stallCounter = stallCount; }
	void manageStalls(void);
	bool detectZeroGapHazard(void) const;
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
	data_t getSP() const { return regsp; }
	data_t getEPC() const { return epc; }
	data_t getECA() const { return eca; }
	bool getIEN() const { return ien; }
	bool getIntPin() const { return int_pin; }
	void setIntPin(bool level) { int_pin = level; }
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
	void setTraceWriter(TraceWriter* tw) { traceWriter = tw; }

	// Statistics
	void dumpStats(std::ostream& out) const;
	const ANEMCounters& getCounters() const { return counters; }

	// Memory access logging
	ANEMDataMemory& getDataMemory() { return dmem; }

	// State write accessors (for snapshot restore)
	void setPC(addr_t p) { pc = p; }
	void setHI(data_t h) { reghi = h; }
	void setLO(data_t l) { reglo = l; }
	void setSP(data_t s) { regsp = s; }
	void setEPC(data_t e) { epc = e; }
	void setECA(data_t e) { eca = e; }
	void setIEN(bool e) { ien = e; }
	void writeRegister(uint8_t reg, data_t val) { regbnk.r_write(reg, val); }
	void setFetchToDecode(const f2d& r) { fetch_to_decode = r; }
	void setDecodeToExec(const d2e& r) { decode_to_exec = r; }
	void setExecToMem(const e2m& r) { exec_to_mem = r; }
	void setMemToWB(const m2w& r) { mem_to_wb = r; }
	void setStallState(bool sif, bool sid, bool sex, bool smem, bool swb, bool smaster, unsigned int counter) {
		p_stall_if = sif; p_stall_id = sid; p_stall_ex = sex;
		p_stall_mem = smem; p_stall_wb = swb; p_stall_master = smaster; stallCounter = counter;
	}
	void setForwardingState(bool aa, bool ab, bool ma, bool mb) {
		fwd_alu_alua = aa; fwd_alu_alub = ab; fwd_mem_alua = ma; fwd_mem_alub = mb;
	}
	void setHaltDetection(unsigned long long tc, addr_t lpc, addr_t ppc, unsigned int spc, int dc) {
		totalCycles = tc; lastPC = lpc; prevPC = ppc; samePCCount = spc; drainCycles = dc;
	}
	bool getZFlag() const { return z_flag; }
	void setZFlag(bool z) { z_flag = z; }
	bool getFwEnable() const { return fw_enable; }
	unsigned int getMaxCycles() const { return maxCycles; }
	unsigned long long getTotalCycles() const { return totalCycles; }
	addr_t getLastPC() const { return lastPC; }
	addr_t getPrevPC() const { return prevPC; }
	unsigned int getSamePCCount() const { return samePCCount; }
	int getDrainCycles() const { return drainCycles; }
	bool getStallIF() const { return p_stall_if; }
	bool getStallID() const { return p_stall_id; }
	bool getStallEX() const { return p_stall_ex; }
	bool getStallMEM() const { return p_stall_mem; }
	bool getStallWB() const { return p_stall_wb; }
	bool getStallMaster() const { return p_stall_master; }
	unsigned int getStallCounter() const { return stallCounter; }
	bool getFwdAluAluA() const { return fwd_alu_alua; }
	bool getFwdAluAluB() const { return fwd_alu_alub; }
	bool getFwdMemAluA() const { return fwd_mem_alua; }
	bool getFwdMemAluB() const { return fwd_mem_alub; }
	ANEMCounters& getCountersMut() { return counters; }
};



#endif /* CPU_H_ */
