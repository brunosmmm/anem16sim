/*
 * alu.h
 *
 *  Created on: 07/12/2014
 *      Author: bruno
 */

#ifndef ALU_H_
#define ALU_H_

#include "mem.h"

#define ANEM_ALU_Z 0x1

enum ANEMAluOp {aluR, aluS, aluNOP, aluREL, aluOFFSET};
enum ANEMAluFunc {aluADD, aluSUB, aluOR, aluAND, aluXOR, aluNOR, aluSLT, aluSHL, aluSHR, aluSAR, aluROL, aluROR};


typedef struct ALU_OUT
{

	data_t value;
	uint8_t flags;

} ANEMAluOut;

class ANEMAlu
{
public:
	ANEMAluOut operate(ANEMAluOp op, uint8_t shamt, ANEMAluFunc func, data_t aluA, data_t aluB);

};

#endif /* ALU_H_ */
