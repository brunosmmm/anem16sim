/*
 * stats.cpp
 *
 *  Created on: 18/12/2014
 *      Author: bruno
 */

#include "stats.h"

void ANEMInstructionCounters::count(uint8_t opcode)
{
	std::map<uint8_t,long long int>::iterator it;

	it = this->instructionCountByOpcode.find(opcode);

	if (it != this->instructionCountByOpcode.end())
	{
		//already exists, increment
		(*it).second++;

	}else
	{

		this->instructionCountByOpcode[opcode] = 1;

	}


}



