/*
 * alu.cpp
 *
 *  Created on: 07/12/2014
 *      Author: bruno
 */

#include "alu.h"

ANEMAluOut ANEMAlu::operate(ANEMAluOp op, uint8_t shamt, ANEMAluFunc func, data_t aluA, data_t aluB)
{

	ANEMAluOut aout;

	aout.flags = 0x0;

	switch(op)
	{
	case aluR:
		switch (func)
		{
			//R TYPE
			case aluADD:
				//signed add?
				//aout.value =
				break;
			case aluSUB:
				aout.value = aluA - aluB;
				break;
			case aluAND:
				aout.value = aluA & aluB;
				break;
			case aluOR:
				aout.value = aluA | aluB;
				break;
			case aluNOR:
				aout.value = ~(aluA | aluB);
				break;
			case aluXOR:
				aout.value = aluA ^ aluB;
				break;
			case aluSLT:
				aout.value = (aluB > aluA) ? 1 : 0;
				break;
			default:
				//exception
				break;
			}
		break;
		case aluS:
			switch(func)
			{
			//S TYPE
			case aluSHR:
				aout.value = aluA >> shamt;
				break;
			case aluSHL:
				aout.value = aluA << shamt;
				break;
			case aluSAR:
				aout.value = aluA / (1<<shamt);
				break;
			case aluROR:
				aout.value = (aluA >> shamt) | (aluA << sizeof(data_t)*8 - shamt);
				break;
			case aluROL:
				aout.value = (aluA << shamt) | (aluA >> sizeof(data_t)*8 - shamt);
				break;
			default:
				//exception
				break;
			}
				break;
	case aluOFFSET:
		aout.value = aluA + aluB;
		break;
	case aluREL:
		//relative jump address for BZ and J
		///@todo verify because i think that currently that is not what happens
		break;
	case aluNOP:
		break;
	}

	//set flags
	if (aout.value == 0)
		aout.flags |= ANEM_ALU_Z;

	return aout;

}


