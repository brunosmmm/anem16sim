/*
 * stats.cpp
 *
 *  Created on: 18/12/2014
 *      Author: bruno
 */

#include "stats.h"
#include "../disasm.h"
#include <iomanip>

void ANEMInstructionCounters::count(uint8_t opcode)
{
	auto it = this->instructionCountByOpcode.find(opcode);

	if (it != this->instructionCountByOpcode.end())
	{
		it->second++;
	} else
	{
		this->instructionCountByOpcode[opcode] = 1;
	}
}

long long int ANEMInstructionCounters::getCount(uint8_t opcode) const
{
	auto it = this->instructionCountByOpcode.find(opcode);
	if (it != this->instructionCountByOpcode.end())
		return it->second;
	return 0;
}

long long int ANEMInstructionCounters::getTotal() const
{
	long long int total = 0;
	for (const auto& kv : this->instructionCountByOpcode)
		total += kv.second;
	return total;
}

void ANEMInstructionCounters::dump(std::ostream& out) const
{
	long long int total = getTotal();
	if (total == 0) return;

	// Reset formatting state
	out << std::dec << std::setfill(' ');

	out << "Instruction mix:" << std::endl;
	for (const auto& kv : this->instructionCountByOpcode)
	{
		std::string name = opcodeName(kv.first);
		double pct = 100.0 * kv.second / total;
		out << "  " << std::left << std::setw(12) << name
		    << std::right << std::setw(8) << std::dec << kv.second
		    << "  (" << std::fixed << std::setprecision(1) << pct << "%)"
		    << std::endl;
	}
}

void ANEMCounters::dumpStats(std::ostream& out) const
{
	long long int totalInstr = this->imix.getTotal();

	// Reset formatting state
	out << std::dec << std::setfill(' ');

	out << "=== Simulation Statistics ===" << std::endl;
	out << "Total cycles:           " << this->cyclecount << std::endl;
	out << "Instructions decoded:   " << totalInstr << std::endl;
	out << "Stall cycles:           " << this->stallCount << std::endl;
	out << "Bubble cycles:          " << this->bubbleCount << std::endl;
	out << "Forwarding events:" << std::endl;
	out << "  ALU -> ALU:           " << this->fwdAluAluCount << std::endl;
	out << "  MEM -> ALU:           " << this->fwdMemAluCount << std::endl;
	out << std::endl;

	this->imix.dump(out);
}
