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

ANEMInstruction ANEMCPU::p_fetch(void)
{
	addr_t npc;
	ANEMInstruction instr;

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

	//set new pc
	this->pc = npc;

	//return instruction
	return instr;

}

struct d2e ANEMCPU::p_decode(ANEMInstruction i)
{

	struct d2e toexec;

	if (this->p_stall_id) return this->decode_to_exec;

	//statistics
	this->counters.instructionDecoded(i);

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

	//register bank control decode
	switch (i.opcode)
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
		//shouldnt be like that but JAL is not clearly implemented currently
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		toexec.j_flag = true;
		break;
	case ANEM_OPCODE_BZ:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		toexec.bz_flag = true;
		break;
	default:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		break;

	}

	//alu function
	if (toexec.alu_ctl == aluR)
	{
		switch(i.func)
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
		switch(i.func)
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

		toexec.alu_shamt = i.shamt;
	}

	//make sure not to write in register 0
	if (i.rega == 0)
	{
		toexec.reg_ctl = regNOP;
	}

	//register selectors
	toexec.rega_sel = i.rega;
	toexec.regb_sel = i.regb;

	//read registers
	toexec.rega_out = this->regbnk.r_read(i.rega);
	toexec.regb_out = this->regbnk.r_read(i.regb);

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
			toexec.j_dest = (addr_t)((int32_t)this->pc + (int16_t)i.address);
		} else if (toexec.bz_flag == true) //BZ jump
		{
			//calculate immediately
			if (this->exec_to_mem.alu_out.flags & ANEM_ALU_Z)
				toexec.j_dest = (addr_t)((int32_t)this->pc + (int16_t)i.address);
			else
				toexec.j_dest = this->pc + 1;
		}

	//immediate
	toexec.imm_val = i.byte;

	//offset
	toexec.off_4 = i.off_4;

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
	default:
		//exception
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
