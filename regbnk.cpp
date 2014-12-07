/*
 * regbnk.cpp
 *
 *  Created on: 07/12/2014
 *      Author: bruno
 */

#include "regbnk.h"

void ANEMRegBnk::r_write(uint8_t reg, data_t value)
{
	//cant write on register 0
	if (reg == 0)
		return;

	//prevent errors
	reg &= (GPR_COUNT-1);

	//write value
	this->registers[reg] = value;

}

data_t ANEMRegBnk::r_read(uint8_t reg)
{

	//register 0 always return 0
	if (reg == 0)
		return 0x0000;

	reg &= (GPR_COUNT-1);

	return this->registers[reg];

}

void ANEMRegBnk::reset(void)
{
	int i = 0;

	for (i = 0; i < GPR_COUNT; i++)
		//this->r_write(i, 0x0000);
		this->registers[i] = 0x0000;

}



