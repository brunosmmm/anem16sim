/****
 * @file cpu.cpp
 * @brief ANEM CPU
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"

static struct PIPELINE_STAGES
{
	struct f2d
	{
		ANEMInstruction ireg;
	} fetch_to_decode;

	struct d2e decode_to_exec;
	struct e2m exec_to_mem;
	struct m2w mem_to_wb;

} pipeRegisters;

ANEMCPU::ANEMCPU(bool fw_enable)
{

	this->fw_enable = fw_enable;


}

void ANEMCPU::reset(void)
{

	//clear data memory
	this->dmem.clearMem();

	//clear registers
	this->regbnk.reset();

	//zero PC
	this->pc = 0x00000000;
}


void ANEMCPU::clockCycle(void)
{
	ANEMInstruction ireg;
	struct d2e dreg;
	struct e2m ereg;
	struct m2w mreg;

	//fetch
	ireg = this->p_fetch(this->pc);

	//decode
	//use last instruction then update to emulate register behavior
	dreg = this->p_decode(pipeRegisters.fetch_to_decode.ireg);
	pipeRegisters.fetch_to_decode.ireg = ireg;

	//execute (ALU)
	ereg = this->p_execute(pipeRegisters.decode_to_exec);
	pipeRegisters.decode_to_exec = dreg;

	//memory
	mreg = this->p_mem(pipeRegisters.exec_to_mem);
	pipeRegisters.exec_to_mem = ereg;

	//writeback
	this->p_writeback(pipeRegisters.mem_to_wb);

}

ANEMInstruction ANEMCPU::p_fetch(addr_t addr)
{

	//set new PC

	//return instruction
	return this->imem.fetch(addr);

}

struct d2e ANEMCPU::p_decode(ANEMInstruction i)
{

	struct d2e toexec;

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
		toexec.reg_ctl = aluR;
		break;
	case ANEM_OPCODE_S:
		toexec.reg_ctl = regLoadALU;
		toexec.alu_ctl = aluS;
		break;
	case ANEM_OPCODE_LW:
		toexec.reg_ctl = regLoadMEM;
		toexec.alu_ctl = aluOFFSET;
		break;
	case ANEM_OPCODE_SW:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluOFFSET;
		break;
	case ANEM_OPCODE_J:
		toexec.reg_ctl = regNOP;
		toexec.reg_ctl = aluREL;
		break;
	case ANEM_OPCODE_JR:
		toexec.reg_ctl = regNOP;
		break;
	case ANEM_OPCODE_JAL:
		//shouldnt be like that but JAL is not clearly implemented currently
		toexec.reg_ctl = regNOP;
		break;
	default:
		toexec.reg_ctl = regNOP;
		toexec.alu_ctl = aluNOP;
		break;

	}

	//alu function
	if (toexec.alu_ctl == aluS)
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


	} else if (toexec.alu_ctl == aluR)
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
	}

	return toexec;

}

struct e2m ANEMCPU::p_execute(struct d2e d)
{

	struct e2m tomem;

	//pass on to later stages
	tomem.reg_ctl = d.reg_ctl;

	//ALU operation
	//tomem.alu_out = this->alu.operate(d.alu_func);

}

struct m2w ANEMCPU::p_mem(struct e2m e)
{

	struct m2w towb;

	//pass data to wb
	towb.reg_ctl = e.reg_ctl;


}

void ANEMCPU::p_writeback(struct m2w m)
{



}

bool ANEMCPU::programEnd(void)
{

	return false; //for now

}
