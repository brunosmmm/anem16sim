/****
 * @file cpu.cpp
 * @brief ANEM CPU
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"

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

	//make sure we are not doing anything before the pipeline is filled
	//after reset
	this->decode_to_exec.alu_ctl = aluNOP;
	this->decode_to_exec.reg_ctl = regNOP;
	this->mem_to_wb.reg_ctl = regNOP;
	this->exec_to_mem.reg_ctl = regNOP;

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


}


void ANEMCPU::clockCycle(void)
{
	ANEMInstruction ireg;
	struct d2e dreg;
	struct e2m ereg;
	struct m2w mreg;

	///@todo no need to use function argument and return values, can manipulate internal structures directly

	//stall control
	this->manageStalls();

	//fetch
	ireg = this->p_fetch();

	//decode
	//use last instruction then update to emulate register behavior
	dreg = this->p_decode(this->fetch_to_decode.ireg);
	this->fetch_to_decode.ireg = ireg;

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

}

struct f2d ANEMCPU::p_fetch(void)
{
	addr_t npc;
	ANEMInstruction instr;
	struct f2d todecode;


	if (this->p_stall_if)
	{
		//stalled
		return this->fetch_to_decode.ireg;
	}


	if (this->decode_to_exec.j_flag || this->decode_to_exec.jr_flag || this->decode_to_exec.bz_flag)
	{
		//jumps
		instr = this->imem.fetch(this->decode_to_exec.j_dest);
		npc = this->decode_to_exec.j_dest + 1;
	} else
	{
		//normal operation
		instr = this->imem.fetch(this->pc);
		npc = this->pc + 1;

	}

	todecode.savedpc = this->pc;
	todecode.ireg = instr;


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

	//disable forwarding
	toexec.fwd_alu_alua = false;
	toexec.fwd_alu_alub = false;
	toexec.fwd_mem_alua = false;
	toexec.fwd_mem_alub = false;

	//saved pc
	toexec.savedpc = i.savedpc;

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
	  //decide here
	  switch(i.ireg.func)
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
		toexec.hictl = noOp;
		toexec.loctl = noOp;
		break;

	      case ANEM_M1FUNC_MFLO:
		toexec.hictl = noOp;
		toexec.loctl = noOp;
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
		///@todo flag an exception
		break;
	    }
	default:
	  //this is an exception!!
	  ///@todo flag an exception
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
	if (i.ireg.rega == 0)
	{
		toexec.reg_ctl = regNOP;
	}

	//register selectors
	toexec.rega_sel = i.ireg.rega;
	toexec.regb_sel = i.ireg.regb;

	//read registers
	toexec.rega_out = this->regbnk.r_read(i.ireg.rega);
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

		}
		else if ((toexec.rega_sel == this->mem_to_wb.rega_sel) && (toexec.rega_sel != 0))
		{
			//forward from mem to exec
			toexec.fwd_mem_alua = true;
			toexec.fwd_alua = this->getFwdValFromMEM();

		}

		if ((toexec.regb_sel == this->exec_to_mem.rega_sel) && (toexec.regb_sel != 0))
		{

			//forward from ALU out to exec
			toexec.fwd_alu_alub = true;
			toexec.fwd_alub = this->getFwdValFromEX();

		}
		else if ((toexec.regb_sel == this->mem_to_wb.rega_sel) && (toexec.regb_sel != 0))
		{
			//forward from mem to exec
			toexec.fwd_mem_alub = true;
			toexec.fwd_alub = this->getFwdValFromMEM();

		}
	}

		//JR JUMP
		if (toexec.jr_flag == true)
		{
			toexec.j_dest = toexec.rega_out;
		} else if (toexec.j_flag == true) //J or JAL jump
		{
			//calculate immediately, no need to use ALU. Signed ADD
			toexec.j_dest = (addr_t)((int32_t)this->pc + (int16_t)i.ireg.address);

		} else if (toexec.bz_flag == true) //BZ jump
		{
			//calculate immediately
			if (this->exec_to_mem.alu_out.flags & ANEM_ALU_Z)
				toexec.j_dest = (addr_t)((int32_t)this->pc + (int16_t)i.ireg.address);
			else
				toexec.j_dest = this->pc + 1;
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

	//verify if memory access is done
	if (e.mem_enable)
	{
		if (e.mem_write)
		{
			//write to memory
			this->dmem.write(e.alu_out.value,e.rega_out);

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
	  //JAL instruction - save PC
	  regval = m.savedpc + 1;
	  break;
	default:
		//exception
		break;

	}

	//calcualte AIS
	ais_result = (((dword_t)m.hiout << 16) | ((dword_t)m.loout)) + (sdword_t)m.imm_val;

	//special registers
	switch(m.hictl)
	  {
	    case loadLower:
	      this->reghi = (m.hiout & 0xFF00) | m.imm_val;
	      break;
	    case loadUpper:
	      this->reghi = (m.hiout & 0x00FF) | (data_t)m.imm_val << sizeof(data_t)*4;
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
	      this->reglo = (m.loout & 0xFF00) | m.imm_val;
	      break;
	    case loadUpper:
	      this->reglo = (m.loout & 0x00FF) | (data_t)m.imm_val << sizeof(data_t)*4;
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
	static unsigned int cycleCount = 0;

	cycleCount++;

	if (cycleCount == 100) return true;

	return false; //for now

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
		break;
	case regLoadBYTELower:

		//calculate value on-the-fly
		regval = this->exec_to_mem.rega_out & 0xFF00;
		regval |= this->exec_to_mem.imm_val;
		return regval;
		break;

	case regLoadBYTEUpper:

		regval = this->exec_to_mem.rega_out & 0xFF00;
		regval |= ((data_t)this->exec_to_mem.imm_val << sizeof(data_t)*4);
		return regval;
		break;

	case regLoadMEM:
		//cannot forward from here! there must be a stall to compensate
		//this occurs in anem16pipe when there is a LW near a register read
		///@todo implement stalling logic in anem16pipe
		this->insertStalls(1); //inserts 1 stall, in next cycle
		break;

	case regNOP:
		//makes no sense at all
		break;
	default:
		//error
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
		break;
	case regLoadBYTELower:

		//calculate value on-the-fly
		regval = this->mem_to_wb.rega_out & 0xFF00;
		regval |= this->mem_to_wb.imm_val;
		return regval;
		break;

	case regLoadBYTEUpper:

		regval = this->mem_to_wb.rega_out & 0xFF00;
		regval |= ((data_t)this->mem_to_wb.imm_val << sizeof(data_t)*4);
		return regval;
		break;

	case regLoadMEM:
		return this->mem_to_wb.mem_out;
		break;

	case regNOP:
		//makes no sense at all
		break;
	default:
		//error
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
