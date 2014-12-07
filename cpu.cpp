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



}

struct e2m ANEMCPU::p_execute(struct d2e d)
{



}

struct m2w ANEMCPU::p_mem(struct e2m e)
{



}

void ANEMCPU::p_writeback(struct m2w m)
{



}

bool ANEMCPU::programEnd(void)
{

	return false; //for now

}
