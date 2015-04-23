/*
 * regbnk.h
 *
 *  Created on: 07/12/2014
 *      Author: bruno
 */

#ifndef REGBNK_H_
#define REGBNK_H_

#include "mem.h"

#define GPR_COUNT 16

enum ANEMRegBnkOp {regLoadALU, regLoadBYTEUpper, regLoadBYTELower, regLoadMEM, regNOP, regPC, regLoadHI, regLoadLO};

class ANEMRegBnk
{
private:
	data_t registers[GPR_COUNT];
public:
	//register manipulation
	void r_write(uint8_t reg, data_t value);
	data_t r_read(uint8_t reg);

	//clear all registers
	void reset(void);


};

#endif /* REGBNK_H_ */
