/****
 * @file cpu.cpp
 * @brief ANEM CPU
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"
#include "disasm.h"
#include <iostream>
#include <iomanip>

/// Sign-extend a 12-bit value to 16-bit signed
/// Hardware does: resize(signed(jdest(11 downto 0)), 16)
static inline int16_t sign_ext_12(uint16_t val)
{
	return (int16_t)((val & 0x800) ? (val | 0xF000) : val);
}

ANEMCPU::ANEMCPU(bool fw_enable)
{

	this->fw_enable = fw_enable;

	this->imem = ANEMInstructionMemory(131072); //128K instruction memory
	this->dmem = ANEMDataMemory(65487); //64K (including virtual memory)

}

void ANEMCPU::reset(void)
{

	//clear data memory
	this->dmem.clearMem();

	//clear registers
	this->regbnk.reset();

	//zero PC
	this->pc = 0x00000000;

	//initialize pipeline
	//we put a NOP instruction to be initially processed by the CPU
	this->fetch_to_decode.ireg = ANEM_INSTRUCTION_NOP;
	this->fetch_to_decode.bubble = false;
	this->fetch_to_decode.pc = 0;

	//make sure we are not doing anything before the pipeline is filled
	//after reset
	this->decode_to_exec.alu_ctl = aluNOP;
	this->decode_to_exec.reg_ctl = regNOP;
	this->decode_to_exec.bubble = true;
	this->decode_to_exec.pc = 0;
	this->decode_to_exec.ireg = ANEM_INSTRUCTION_NOP;

	this->mem_to_wb.reg_ctl = regNOP;
	this->mem_to_wb.bubble = true;
	this->mem_to_wb.pc = 0;
	this->mem_to_wb.ireg = ANEM_INSTRUCTION_NOP;

	this->exec_to_mem.reg_ctl = regNOP;
	this->exec_to_mem.bubble = true;
	this->exec_to_mem.pc = 0;
	this->exec_to_mem.ireg = ANEM_INSTRUCTION_NOP;

	//disable stalls
	this->stallCounter = 0;
	this->p_stall_if = false;
	this->p_stall_id = false;
	this->p_stall_ex = false;
	this->p_stall_mem = false;
	this->p_stall_wb = false;
	this->p_stall_master = false;

	//reset counters
	this->counters.reset();

	// Reset halt-detection state
	this->totalCycles = 0;
	this->lastPC = 0;
	this->prevPC = 0;
	this->samePCCount = 0;
	this->drainCycles = -1;

}


void ANEMCPU::clockCycle(void)
{
	struct f2d freg;
	struct d2e dreg;
	struct e2m ereg;
	struct m2w mreg;

	///@todo no need to use function argument and return values, can manipulate internal structures directly

	//stall control
	this->manageStalls();

	//fetch
	freg = this->p_fetch();

	//decode
	//use last instruction then update to emulate register behavior
	dreg = this->p_decode(this->fetch_to_decode);
	this->fetch_to_decode = freg;

	// SIM-BUG-6 fix: When IF is stalled, inject NOP bubble into pipeline.
	// Hardware: p_s_alu_aluctl_mux forces ALU_OP to "000" and
	// p_s_wb_regctl_mux to "000" during stalls. Without this, the
	// stalled instruction gets decoded (and later executed) twice.
	if (this->p_stall_if)
	{
		dreg.reg_ctl = regNOP;
		dreg.alu_ctl = aluNOP;
		dreg.mem_enable = false;
		dreg.mem_write = false;
		dreg.j_flag = false;
		dreg.jr_flag = false;
		dreg.bz_flag = false;
		dreg.bhleq_flag = false;
		dreg.hictl = noOp;
		dreg.loctl = noOp;
		dreg.bubble = true;
		this->counters.countBubble();
	}

	//execute (ALU)
	ereg = this->p_execute(this->decode_to_exec);
	this->decode_to_exec = dreg;

	//memory
	mreg = this->p_mem(this->exec_to_mem);
	this->exec_to_mem = ereg;

	//writeback
	this->p_writeback(this->mem_to_wb);
	this->mem_to_wb = mreg;

	//count clock cycles
	this->counters.clockCycle();

	// Invoke trace callback if set
	if (this->traceCallback)
	{
		this->traceCallback(this->counters.getCycleCount(), this->pc,
		                    this->fetch_to_decode.ireg, *this);
	}
}

struct f2d ANEMCPU::p_fetch(void)
{
	addr_t npc;
	addr_t fetchpc; // actual address we fetch from
	ANEMInstruction instr;
	struct f2d todecode;


	if (this->p_stall_if)
	{
		//stalled
		this->counters.countStall();
		return this->fetch_to_decode;
	}


	if (this->decode_to_exec.j_flag || this->decode_to_exec.jr_flag)
	{
		//unconditional jumps (J, JAL, JR) — dest already computed in decode
		fetchpc = this->decode_to_exec.j_dest;
		instr = this->imem.fetch(fetchpc);
		npc = fetchpc + 1;
	} else if (this->decode_to_exec.bz_flag)
	{
		// SIM-BUG-7 fix: Resolve BZ condition here using exec_to_mem Z flag.
		// At this point BZ is in EX stage (decode_to_exec), and exec_to_mem
		// holds the ALU result from instruction N-1 (matching hardware's
		// p_alu_mem_z_2 which is checked when BZ reaches ALU stage).
		if (this->exec_to_mem.alu_out.flags & ANEM_ALU_Z)
		{
			fetchpc = (addr_t)((int32_t)this->pc + (int32_t)sign_ext_12(this->decode_to_exec.bz_offset));
			instr = this->imem.fetch(fetchpc);
			npc = fetchpc + 1;
		}
		else
		{
			fetchpc = this->pc;
			instr = this->imem.fetch(fetchpc);
			npc = fetchpc + 1;
		}
	} else if (this->decode_to_exec.bhleq_flag)
	{
		// BHLEQ: branch if HI == LO, resolved here for same timing reason
		if (this->reghi == this->reglo)
		{
			fetchpc = (addr_t)((int32_t)this->pc + (int32_t)sign_ext_12(this->decode_to_exec.bz_offset));
			instr = this->imem.fetch(fetchpc);
			npc = fetchpc + 1;
		}
		else
		{
			fetchpc = this->pc;
			instr = this->imem.fetch(fetchpc);
			npc = fetchpc + 1;
		}
	} else
	{
		//normal operation
		fetchpc = this->pc;
		instr = this->imem.fetch(fetchpc);
		npc = fetchpc + 1;

	}

	todecode.savedpc = fetchpc;
	todecode.ireg = instr;
	todecode.pc = fetchpc;
	todecode.bubble = false;

	//set new pc
	this->pc = npc;

	//return instruction
	return todecode;

}

struct d2e ANEMCPU::p_decode(struct f2d i)
{

	struct d2e toexec;

	if (this->p_stall_id) return this->decode_to_exec;

	//statistics
	this->counters.instructionDecoded(i.ireg);

	///@todo these structs could be converted to classes that use the previous pipeline stage as an argument to the
	///constructor, so no initialization is needed

	//initialize
	toexec.mem_enable = false;
	toexec.mem_write = false;
	toexec.j_flag = false;
	toexec.jr_flag = false;
	toexec.bz_flag = false;
	toexec.bhleq_flag = false;
	toexec.bz_offset = 0;
	toexec.hictl = noOp;
	toexec.loctl = noOp;
	toexec.bubble = false;

	//disable forwarding
	toexec.fwd_alu_alua = false;
	toexec.fwd_alu_alub = false;
	toexec.fwd_mem_alua = false;
	toexec.fwd_mem_alub = false;

	//saved pc
	toexec.savedpc = i.savedpc;

	// Track PC and instruction for this stage
	toexec.pc = i.pc;
	toexec.ireg = i.ireg;

	//register bank control decode
	switch (i.ireg.opcode)
	{
	case ANEM_OPCODE_LIU:
		toexec.reg_ctl  = regLoadBYTEUpper;
		toexec.alu_ctl = aluNOP;
		break;
	case ANEM_OPCODE_LIL:
		toexec.reg_ctl = regLoadBYTELower;
		toexec.alu_ctl = aluNOP;
		break;
	case ANEM_OPCODE_R:
		toexec.reg_ctl = regLoadALU;
		toexec.alu_ctl = aluR;
		break;
	case ANEM_OPCODE_S:
		toexec.reg_ctl = regLoadALU;
		toexec.alu_ctl = aluS;
		break;
	case ANEM_OPCODE_LW:
		toexec.reg_ctl = regLoadMEM;
		toexec.alu_ctl = aluOFFSET;
		//memory access
		toexec.mem_enable = true;
		break;
	case ANEM_OPCODE_SW:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluOFFSET;
		//memory access
		toexec.mem_enable = true;
		toexec.mem_write = true;
		break;
	case ANEM_OPCODE_J:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluREL;
		toexec.j_flag = true;
		break;
	case ANEM_OPCODE_JR:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		toexec.jr_flag = true;
		break;
	case ANEM_OPCODE_JAL:
		toexec.reg_ctl = regPC;
		toexec.alu_ctl = aluNOP;
		toexec.j_flag = true;
		break;
	case ANEM_OPCODE_BZ:
	case ANEM_OPCODE_BZ_T:
	case ANEM_OPCODE_BZ_N:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		toexec.bz_flag = true;
		break;
	case ANEM_OPCODE_BHLEQ:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		toexec.bhleq_flag = true;
		break;
	case ANEM_OPCODE_M1:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		// M1 sub-function is in bits[11:8] (rega field), not bits[3:0] (func field).
		// Hardware: alias m1op : slv(3 downto 0) is instruction(11 downto 8);
		switch(i.ireg.rega)
		{
		case ANEM_M1FUNC_LLL:
			toexec.hictl = noOp;
			toexec.loctl = loadLower;
			break;
		case ANEM_M1FUNC_LLH:
			toexec.hictl = noOp;
			toexec.loctl = loadUpper;
			break;
		case ANEM_M1FUNC_LHL:
			toexec.hictl = loadLower;
			toexec.loctl = noOp;
			break;
		case ANEM_M1FUNC_LHH:
			toexec.hictl = loadUpper;
			toexec.loctl = noOp;
			break;
		case ANEM_M1FUNC_AIS:
			toexec.hictl = doAIS;
			toexec.loctl = doAIS;
			break;
		case ANEM_M1FUNC_AIL:
			toexec.hictl = noOp;
			toexec.loctl = doAIH_AIL;
			break;
		case ANEM_M1FUNC_AIH:
			toexec.hictl = doAIH_AIL;
			toexec.loctl = noOp;
			break;
		case ANEM_M1FUNC_MFHI:
			// HW-BUG-5: Hardware has no regbnk_ctl for M1 opcode,
			// so MFHI/MFLO never write to GPR. Simulator implements
			// correct behavior.
			toexec.reg_ctl = regLoadHI;
			break;
		case ANEM_M1FUNC_MFLO:
			toexec.reg_ctl = regLoadLO;
			break;
		case ANEM_M1FUNC_MTHI:
			toexec.hictl = fromRegister;
			toexec.loctl = noOp;
			break;
		case ANEM_M1FUNC_MTLO:
			toexec.hictl = noOp;
			toexec.loctl = fromRegister;
			break;
		default:
			break;
		}
		break;
	default:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		break;

	}

	//alu function
	if (toexec.alu_ctl == aluR)
	{
		switch(i.ireg.func)
		{
		case ANEM_FUNC_ADD:
			toexec.alu_func = aluADD;
			break;
		case ANEM_FUNC_OR:
			toexec.alu_func = aluOR;
			break;
		case ANEM_FUNC_XOR:
			toexec.alu_func = aluXOR;
			break;
		case ANEM_FUNC_SUB:
			toexec.alu_func = aluSUB;
			break;
		case ANEM_FUNC_SLT:
			toexec.alu_func = aluSLT;
			break;
	        case ANEM_FUNC_SGT:
		  toexec.alu_func = aluSGT;
		  break;
		case ANEM_FUNC_MUL:
		  toexec.alu_func = aluMUL;
		  break;
		case ANEM_FUNC_NOR:
			toexec.alu_func = aluNOR;
			break;
		case ANEM_FUNC_AND:
			toexec.alu_func = aluAND;
			break;

		default:
			break;
		}


	} else if (toexec.alu_ctl == aluS)
	{
		switch(i.ireg.func)
		{
		case ANEM_FUNC_SHL:
			toexec.alu_func = aluSHL;
			break;
		case ANEM_FUNC_SHR:
			toexec.alu_func = aluSHR;
			break;
		case ANEM_FUNC_SAR:
			toexec.alu_func = aluSAR;
			break;
		case ANEM_FUNC_ROL:
			toexec.alu_func = aluROL;
			break;
		case ANEM_FUNC_ROR:
			toexec.alu_func = aluROR;
			break;

		default:
			break;
		}

		toexec.alu_shamt = i.ireg.shamt;
	}

	//make sure not to write in register 0
	//Exception: JAL (regPC) always writes to R15, regardless of rega field
	if (i.ireg.rega == 0 && toexec.reg_ctl != regPC)
	{
		toexec.reg_ctl = regNOP;
	}

	//register selectors
	// M1 MFHI/MFLO/MTHI/MTLO: dest/src register is in bits[3:0] (func), not bits[11:8] (rega)
	// Hardware: regbnk_sela uses instruction(3 downto 0) for these M1 sub-functions
	if (i.ireg.opcode == ANEM_OPCODE_M1 &&
	    (i.ireg.rega == ANEM_M1FUNC_MFHI || i.ireg.rega == ANEM_M1FUNC_MFLO ||
	     i.ireg.rega == ANEM_M1FUNC_MTHI || i.ireg.rega == ANEM_M1FUNC_MTLO))
	{
		toexec.rega_sel = i.ireg.func;
		toexec.rega_out = this->regbnk.r_read(i.ireg.func);
	}
	else
	{
		toexec.rega_sel = i.ireg.rega;
		toexec.rega_out = this->regbnk.r_read(i.ireg.rega);
	}
	toexec.regb_sel = i.ireg.regb;

	//read registers (regb always from bits[7:4])
	toexec.regb_out = this->regbnk.r_read(i.ireg.regb);

	//read hi/lo
	toexec.hiout = this->reghi;
	toexec.loout = this->reglo;

	//verify if forwarding is necessary

	//detect RAW
	if ((this->exec_to_mem.reg_ctl != regNOP) || (this->mem_to_wb.reg_ctl != regNOP))
	{
		if ((toexec.rega_sel == this->exec_to_mem.rega_sel) && (toexec.rega_sel != 0))
		{

			//forward from ALU out to exec
			toexec.fwd_alu_alua = true;
			toexec.fwd_alua = this->getFwdValFromEX();
			this->counters.countForwardAluAlu();

		}
		else if ((toexec.rega_sel == this->mem_to_wb.rega_sel) && (toexec.rega_sel != 0))
		{
			//forward from mem to exec
			toexec.fwd_mem_alua = true;
			toexec.fwd_alua = this->getFwdValFromMEM();
			this->counters.countForwardMemAlu();

		}

		if ((toexec.regb_sel == this->exec_to_mem.rega_sel) && (toexec.regb_sel != 0))
		{

			//forward from ALU out to exec
			toexec.fwd_alu_alub = true;
			toexec.fwd_alub = this->getFwdValFromEX();
			this->counters.countForwardAluAlu();

		}
		else if ((toexec.regb_sel == this->mem_to_wb.rega_sel) && (toexec.regb_sel != 0))
		{
			//forward from mem to exec
			toexec.fwd_mem_alub = true;
			toexec.fwd_alub = this->getFwdValFromMEM();
			this->counters.countForwardMemAlu();

		}
	}

		//JR JUMP — use forwarded rega value when available
		if (toexec.jr_flag == true)
		{
			if (toexec.fwd_alu_alua || toexec.fwd_mem_alua)
				toexec.j_dest = toexec.fwd_alua;
			else
				toexec.j_dest = toexec.rega_out;
		} else if (toexec.j_flag == true) //J or JAL jump
		{
			// SIM-BUG-3 fix: sign-extend 12-bit offset (hardware does
			// resize(signed(jdest(11 downto 0)),16) in ifetch.vhd)
			// SIM-BUG-8 fix: use savedpc+1 instead of this->pc.
			// At decode time this->pc is instruction_addr+2 (both J fetch
			// and delay slot fetch advanced it), but the assembler computes
			// offset = target - current - 1, expecting current+1 as base.
			toexec.j_dest = (addr_t)((int32_t)(i.savedpc + 1) + (int32_t)sign_ext_12(i.ireg.address));

		} else if (toexec.bz_flag == true || toexec.bhleq_flag == true)
		{
			// SIM-BUG-7 fix: Don't resolve BZ/BHLEQ condition here.
			// Store the branch offset; condition is resolved in p_fetch()
			// one cycle later, when exec_to_mem holds the correct Z flag
			// (from instruction N-1, matching hardware's p_alu_mem_z_2).
			toexec.bz_offset = i.ireg.address;
		}

	//immediate
	toexec.imm_val = i.ireg.byte;

	//offset
	toexec.off_4 = i.ireg.off_4;

	return toexec;

}

struct e2m ANEMCPU::p_execute(struct d2e d)
{

	struct e2m tomem;
	data_t aluA, aluB;

	if (this->p_stall_ex) return this->exec_to_mem;

	//pass on to later stages
	tomem.reg_ctl = d.reg_ctl;
	tomem.mem_enable = d.mem_enable;
	tomem.mem_write = d.mem_write;
	tomem.imm_val = d.imm_val;
	tomem.rega_sel = d.rega_sel;
	tomem.rega_out = d.rega_out;
	tomem.hiout = d.hiout;
	tomem.loout = d.loout;
	tomem.hictl = d.hictl;
	tomem.loctl = d.loctl;
	tomem.savedpc = d.savedpc;
	tomem.pc = d.pc;
	tomem.ireg = d.ireg;
	tomem.bubble = d.bubble;

	//forwarding
	if (this->fw_enable)
	{
		if (d.fwd_alu_alua || d.fwd_mem_alua)
			aluA = d.fwd_alua;
		else
			aluA = d.rega_out;

		if (d.fwd_alu_alub || d.fwd_mem_alub)
			aluB = d.fwd_alub;
		else
			aluB = d.regb_out;

		// Forward store data for SW: rega_out carries the data to store,
		// but forwarding only updates aluA which gets overridden by off_4.
		// The hardware forwarding mux also feeds the store data path.
		if (d.mem_write && (d.fwd_alu_alua || d.fwd_mem_alua))
			tomem.rega_out = d.fwd_alua;

	} else
	{

		///@todo must insert stalls if forwarding is disabled
		this->insertStalls(2); //MEM, WB stages

	}

	//memory accesses, must calculate offset. ALU operand A is used for the offset input
	if (d.mem_enable)
		aluA = d.off_4;

	//ALU operation
	tomem.alu_out = this->alu.operate(d.alu_ctl,d.alu_shamt,d.alu_func,aluA,aluB);

	return tomem;

}

struct m2w ANEMCPU::p_mem(struct e2m e)
{

	struct m2w towb;

	if (this->p_stall_mem) return this->mem_to_wb;

	//pass data to wb
	towb.reg_ctl = e.reg_ctl;
	towb.alu_out = e.alu_out;
	towb.imm_val = e.imm_val;
	towb.rega_sel = e.rega_sel;
	towb.rega_out = e.rega_out;
	towb.hiout = e.hiout;
	towb.loout = e.loout;
	towb.hictl = e.hictl;
	towb.loctl = e.loctl;
	towb.savedpc = e.savedpc;
	towb.pc = e.pc;
	towb.ireg = e.ireg;
	towb.bubble = e.bubble;

	//verify if memory access is done
	if (e.mem_enable)
	{
		if (e.mem_write)
		{
			//write to memory
			this->dmem.write(e.alu_out.value,e.rega_out);

			// HW trace: log memory write
			if (this->traceWriter)
				this->traceWriter->memoryWrite(e.alu_out.value, e.rega_out);

		} else
		{
			//reads from memory
			towb.mem_out = this->dmem.read(e.alu_out.value);
		}

	}

	return towb;

}

void ANEMCPU::p_writeback(struct m2w m)
{
	data_t regval;
	sdword_t ais_result;

	if (this->p_stall_wb) return;

	//writeback operation
	switch(m.reg_ctl)
	{
	case regLoadALU:
		this->regbnk.r_write(m.rega_sel,m.alu_out.value);
		break;
	case regLoadMEM:
		this->regbnk.r_write(m.rega_sel,m.mem_out);
		break;
	case regLoadBYTELower:
		regval = this->regbnk.r_read(m.rega_sel);
		regval &= 0xFF00;
		regval |= m.imm_val;
		this->regbnk.r_write(m.rega_sel,regval);
		break;
	case regLoadBYTEUpper:
		regval = this->regbnk.r_read(m.rega_sel);
		regval &= 0x00FF;
		regval |= ((data_t)m.imm_val) << sizeof(data_t)*4;
		this->regbnk.r_write(m.rega_sel,regval);
		break;
	case regNOP:
		break;

	case regPC:
		// SIM-BUG-1 fix: JAL must write return address to R15
		// Hardware: regbnk_ctl "101" writes PC_IN to register 15
		this->regbnk.r_write(15, m.savedpc + 1);
		break;
	case regLoadHI:
		// MFHI: move HI register to GPR (pipeline-captured value, same as HW)
		this->regbnk.r_write(m.rega_sel, m.hiout);
		break;
	case regLoadLO:
		// MFLO: move LO register to GPR (pipeline-captured value, same as HW)
		this->regbnk.r_write(m.rega_sel, m.loout);
		break;
	default:
		//exception
		break;

	}

	//calcualte AIS
	ais_result = (((dword_t)m.hiout << 16) | ((dword_t)m.loout)) + (sdword_t)m.imm_val;

	//special registers
	// For loadLower/loadUpper (LHL/LHH/LLL/LLH): use live register value,
	// not pipeline-captured m.hiout/m.loout. Hardware's RegANEMB preserves
	// its own current output bits, so back-to-back LHH+LHL works correctly.
	switch(m.hictl)
	  {
	    case loadLower:
	      this->reghi = (this->reghi & 0xFF00) | m.imm_val;
	      break;
	    case loadUpper:
	      this->reghi = (this->reghi & 0x00FF) | (data_t)m.imm_val << sizeof(data_t)*4;
	      break;
	    case doAIH_AIL:
	      this->reghi = m.hiout + (sdata_t)m.imm_val;
	      break;

	    case doAIS:
	      this->reghi = (data_t)(ais_result >> 16);
	      break;

	    case fromRegister:
	      this->reghi = m.rega_out;
	      break;

	    default:
	      break;
	  }

	switch(m.loctl)
	  {
	    case loadLower:
	      this->reglo = (this->reglo & 0xFF00) | m.imm_val;
	      break;
	    case loadUpper:
	      this->reglo = (this->reglo & 0x00FF) | (data_t)m.imm_val << sizeof(data_t)*4;
	      break;
	    case doAIH_AIL:
	      this->reglo = m.loout + (sdata_t)m.imm_val;
	      break;

	    case doAIS:
	      this->reglo = (data_t)(ais_result);
	      break;

	    case fromRegister:
	      this->reglo = m.rega_out;
	      break;

	    default:
	      break;
	  }

	//done!

}

bool ANEMCPU::programEnd(void)
{
	this->totalCycles++;

	// Safety: max cycle count
	if (this->totalCycles >= this->maxCycles) return true;

	// If draining, count down and return true when done
	if (this->drainCycles >= 0)
	{
		if (this->drainCycles == 0) return true;
		this->drainCycles--;
		return false;
	}

	// Detect termination: PC stuck in a small loop for N cycles.
	// A "J self" with delay slot causes PC to alternate between addr and
	// addr+1, so we track a small window of recent PCs.
	if (this->pc == this->lastPC || this->pc == this->prevPC)
	{
		this->samePCCount++;
		if (this->samePCCount >= 8)
		{
			// Start pipeline drain
			this->drainCycles = 4;
			return false;
		}
	}
	else
	{
		this->samePCCount = 0;
	}
	this->prevPC = this->lastPC;
	this->lastPC = this->pc;

	return false;

}

void ANEMCPU::loadProgram(std::string fileName)
{

	this->imem.loadProgram(fileName);

	this->reset();

}

data_t ANEMCPU::getFwdValFromEX(void)
{
	data_t regval = 0x0000;

	switch (this->exec_to_mem.reg_ctl)
	{
	case regLoadALU:
		return this->exec_to_mem.alu_out.value;

	case regLoadBYTELower:
		// SIM-BUG-4 (HW-BUG-4): Forward the byte-loaded value,
		// not undefined ALU output
		regval = this->exec_to_mem.rega_out & 0xFF00;
		regval |= this->exec_to_mem.imm_val;
		return regval;

	case regLoadBYTEUpper:
		regval = this->exec_to_mem.rega_out & 0x00FF;
		regval |= ((data_t)this->exec_to_mem.imm_val << sizeof(data_t)*4);
		return regval;

	case regLoadMEM:
		//cannot forward from here! there must be a stall to compensate
		//this occurs in anem16pipe when there is a LW near a register read
		this->insertStalls(1); //inserts 1 stall, in next cycle
		break;

	case regPC:
		// JAL: forward saved PC + 1 (return address written to R15)
		return this->exec_to_mem.savedpc + 1;

	case regLoadHI:
		return this->exec_to_mem.hiout;

	case regLoadLO:
		return this->exec_to_mem.loout;

	case regNOP:
		break;
	}

	return 0xFFFF;

}

data_t ANEMCPU::getFwdValFromMEM(void)
{
	data_t regval = 0x0000;

	switch (this->mem_to_wb.reg_ctl)
	{
	case regLoadALU:
		return this->mem_to_wb.alu_out.value;

	case regLoadBYTELower:
		regval = this->mem_to_wb.rega_out & 0xFF00;
		regval |= this->mem_to_wb.imm_val;
		return regval;

	case regLoadBYTEUpper:
		regval = this->mem_to_wb.rega_out & 0x00FF;
		regval |= ((data_t)this->mem_to_wb.imm_val << sizeof(data_t)*4);
		return regval;

	case regLoadMEM:
		return this->mem_to_wb.mem_out;

	case regPC:
		return this->mem_to_wb.savedpc + 1;

	case regLoadHI:
		return this->mem_to_wb.hiout;

	case regLoadLO:
		return this->mem_to_wb.loout;

	case regNOP:
		break;
	}

	return 0xFFFF;

}

void ANEMCPU::manageStalls(void)
{

	//executed every clock cycle
	if (this->stallCounter > 0)
	{
		this->p_stall_if = true;
		this->stallCounter--;
	}
	else if (this->p_stall_master == false)
			this->p_stall_if = false;

}

void ANEMCPU::dumpRegisters(void)
{
	std::cout << "=== Registers ===" << std::endl;
	std::cout << "PC = 0x" << std::hex << std::setfill('0') << std::setw(4) << this->pc << std::endl;
	for (int i = 0; i < GPR_COUNT; i++)
	{
		std::cout << "R" << std::dec << std::setw(2) << i << " = 0x"
		          << std::hex << std::setfill('0') << std::setw(4)
		          << this->regbnk.r_read(i) << std::endl;
	}
	std::cout << "HI = 0x" << std::hex << std::setfill('0') << std::setw(4) << this->reghi << std::endl;
	std::cout << "LO = 0x" << std::hex << std::setfill('0') << std::setw(4) << this->reglo << std::endl;
}

void ANEMCPU::dumpMemory(addr_t start, addr_t count)
{
	std::cout << "=== Data Memory [" << std::dec << start << ".." << (start+count-1) << "] ===" << std::endl;
	for (addr_t i = start; i < start + count; i++)
	{
		std::cout << "  [" << std::dec << std::setw(4) << i << "] = 0x"
		          << std::hex << std::setfill('0') << std::setw(4)
		          << this->dmem.read(i) << std::endl;
	}
}

void ANEMCPU::dumpStats(std::ostream& out) const
{
	this->counters.dumpStats(out);
}
