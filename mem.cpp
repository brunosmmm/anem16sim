/*
 * @file mem.cpp
 * @brief ANEM data and instruction memory emulation
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "mem.h"
#include <cstring>
#include <regex>
#include "except.h"

/***
 * @brief Allocates memory for the instruction mem
 * @param size Memory size in words
 */
ANEMInstructionMemory::ANEMInstructionMemory(uint32_t size)
{

	this->size = size;
	///allocate
	this->imem = new ANEMInstruction[size];

	///initialize to zeros
	memset((void*)this->imem,0x00,sizeof(ANEMInstruction)*size);

}

ANEMDataMemory::ANEMDataMemory(uint32_t size)
{
	this->size = size;

	//allocate
	this->dmem = new dmem_t[size];

	//clear memory
	this->clearMem();

}

void ANEMDataMemory::clearMem(void)
{
	//set all to zeros
	memset((void*)this->dmem,0x0000,sizeof(dmem_t)*this->size);

}

data_t ANEMDataMemory::read(addr_t address)
{

	std::map<addr_t, ANEMMemMappedPeripheral*>::iterator it;

	if (address > this->size)
	{

		//look into peripherals
		it = this->vmem.find(address);

		if (it != this->vmem.end())
		{

			return this->vmem[address]->read(address);

		}

		//not found!

		return 0xFFFF;
	}

	return this->dmem[address];

}

void ANEMDataMemory::write(addr_t address, dmem_t data)
{

	std::map<addr_t, ANEMMemMappedPeripheral*>::iterator it;

	if (address > this->size)
	{

		//look into peripherals
		it = this->vmem.find(address);

		if (it != this->vmem.end())
		{

			this->vmem[address]->write(address,data);

		}

		//not found!


		return;
	}

	this->dmem[address] = data;

}

ANEMInstruction ANEMInstructionMemory::fetch(addr_t addr)
{

	if (addr > this->size)
	{
		//exception, do something here
		return ANEM_INSTRUCTION_NOP;
	}

	return this->imem[addr];

}

void ANEMInstructionMemory::loadProgram(std::string fileName)
{
	std::ifstream file(fileName);
	std::string line;
	std::smatch sm;

	//hex file format regex
	std::regex ihex("^:([a-fA-F0-9]{2})([a-fA-F0-9]{4})([a-fA-F0-9]{2})([a-fA-F0-9]*)([a-fA-F0-9]{2})");
	//binary format regex
	std::regex bin("([01]+)\\t([01]{16})$");

	//line count
	unsigned int i = 0;
	bool ihex_f = false, bin_f = false;

	if (file.is_open() == false)
	{

		//bad
		throw ANEM_PROGRAM_LOAD_EXCEPT;

	}

	while (std::getline(file,line))
	{

		if (i == 0)
		{
			//first line, try to match format
			std::regex_match(line,sm,ihex);

			if (sm.size() > 0)
			{

				//ihex format
				ihex_f = true;
			}
			else
			{

				std::regex_match(line,sm,bin);

				if (sm.size() > 0)
				{

					//bin format
					bin_f = true;
				}

			}

		}

		//parse line and add instructions
		if (ihex_f)
		{

			//parse ihex
			std::regex_match(line,sm,ihex);

			//first group is data field size
			unsigned int d_size = std::stoi(sm[1].str(),nullptr,16);

			//second group is starting address
			uint32_t s_addr = std::stoi(sm[2].str(),nullptr,16);

			//third group is data type
			unsigned int d_type = std::stoi(sm[3].str(),nullptr,16);

			//verify data type
			if (d_type == 1)
			{

				//end of file
				return;

			}

			//fourth group is instructions, must divide into substrings and convert
			std::string sub;
			unsigned int substr = 0;
			while (d_size > 0)
			{

				sub = sm[4].str().substr(substr,2);

				this->imem[s_addr++] = ANEMInstruction(std::stoi(sub,nullptr,16));

				substr += 2;

				d_size -= 2;

			}

			//fifth group is checksum
			unsigned int d_checksum = std::stoi(sm[5].str(),nullptr,16);

		} else
		{

			if (bin_f)
			{

				//parse bin
				std::regex_match(line,sm,bin);

				//first group is the address
				addr_t i_addr = std::stoi(sm[1].str(),nullptr,2);

				//second group is the instruction
				this->imem[i_addr] = ANEMInstruction(std::stoi(sm[2].str(),nullptr,2));

			}
			else
			{

				//dont know what is this, exception
				///@todo insert exception here

			}

		}

		i++;

	}


}

bool ANEMDataMemory::attachPeripheral(addr_t address, ANEMMemMappedPeripheral p)
{

	std::map<addr_t, ANEMMemMappedPeripheral*>::iterator it;
	unsigned int a_range = 0;

	//must be outside of memory range
	if (address <= this->size) return false;

	//verify if this address is available
	it = this->vmem.find(address);

	if (it != this->vmem.end())
	{

		//already allocated, fail
		return false;

	}

	//check if the peripheral fits in the address space
	it = this->vmem.find(address+p.getLength());
	if (it != this->vmem.end())
	{

		//clash
		return false;

	}

	//all good, set base address
	p.setBaseAddress(address);

	//register all addresses in range
	a_range = p.getLength();

	while (a_range > 0)
	{
		this->vmem[address+(--a_range)] = &p;
	}

	return true;
}
