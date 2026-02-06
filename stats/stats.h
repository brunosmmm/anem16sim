/*
 * stats.h
 *
 *  Created on: 18/12/2014
 *      Author: bruno
 */

#ifndef STATS_STATS_H_
#define STATS_STATS_H_

#include "../instrset.h"
#include "../types.h"
#include <map>
#include <iostream>
#include <string>

class ANEMInstructionCounters
{
private:
	std::map<uint8_t,long long int> instructionCountByOpcode;

public:
	void count(uint8_t opcode);
	long long int getCount(uint8_t opcode) const;
	long long int getTotal() const;
	void reset(void) { this->instructionCountByOpcode.clear(); }
	void dump(std::ostream& out) const;
};

class ANEMCounters
{
private:
	ANEMInstructionCounters imix;
	long long int cyclecount = 0;

	// Forwarding/stall/bubble counters
	long long int fwdAluAluCount = 0;
	long long int fwdMemAluCount = 0;
	long long int stallCount = 0;
	long long int bubbleCount = 0;

public:

	void reset(void) {
		this->cyclecount = 0;
		this->imix.reset();
		this->fwdAluAluCount = 0;
		this->fwdMemAluCount = 0;
		this->stallCount = 0;
		this->bubbleCount = 0;
	}

	void clockCycle(void) { this->cyclecount++; }
	void instructionDecoded(ANEMInstruction i) { this->imix.count(i.opcode); }

	long long int getCycleCount() const { return this->cyclecount; }

	void countForwardAluAlu() { this->fwdAluAluCount++; }
	void countForwardMemAlu() { this->fwdMemAluCount++; }
	void countStall() { this->stallCount++; }
	void countBubble() { this->bubbleCount++; }

	long long int getStallCount() const { return stallCount; }
	long long int getBubbleCount() const { return bubbleCount; }
	long long int getForwardAluAluCount() const { return fwdAluAluCount; }
	long long int getForwardMemAluCount() const { return fwdMemAluCount; }

	void dumpStats(std::ostream& out) const;
};




#endif /* STATS_STATS_H_ */
