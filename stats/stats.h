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

class ANEMInstructionCounters
{
private:
	std::map<uint8_t,long long int> instructionCountByOpcode;

public:
	void count(uint8_t opcode);

	long long int getCount(uint8_t opcode);

	void reset(void) { this->instructionCountByOpcode.clear(); }

};

class ANEMCounters
{
private:
	ANEMInstructionCounters imix;
	long long int cyclecount = 0;
public:

	void reset(void) { this->cyclecount = 0; this->imix.reset(); }

	void clockCycle(void) { this->cyclecount++; }
	void instructionDecoded(ANEMInstruction i) { this->imix.count(i.opcode); }

};




#endif /* STATS_STATS_H_ */
