/*
 * stats.cpp
 *
 *  Created on: 18/12/2014
 *      Author: bruno
 */

#include "stats.h"
#include "../disasm.h"
#include <iomanip>

using json = nlohmann::json;

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

json ANEMCounters::toJson() const
{
	json j;
	j["cycles"] = cyclecount;
	j["stalls"] = stallCount;
	j["bubbles"] = bubbleCount;
	j["fwd_alu_alu"] = fwdAluAluCount;
	j["fwd_mem_alu"] = fwdMemAluCount;

	json mix;
	for (const auto& kv : imix.getMap())
		mix[std::to_string(kv.first)] = kv.second;
	j["instruction_mix"] = mix;

	return j;
}

void ANEMCounters::fromJson(const json& j)
{
	cyclecount = j.value("cycles", (long long int)0);
	stallCount = j.value("stalls", (long long int)0);
	bubbleCount = j.value("bubbles", (long long int)0);
	fwdAluAluCount = j.value("fwd_alu_alu", (long long int)0);
	fwdMemAluCount = j.value("fwd_mem_alu", (long long int)0);

	if (j.contains("instruction_mix"))
	{
		std::map<uint8_t, long long int> m;
		for (auto& [key, val] : j["instruction_mix"].items())
			m[(uint8_t)std::stoul(key)] = val.get<long long int>();
		imix.setMap(m);
	}
}
