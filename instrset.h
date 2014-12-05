/*
 * @file instrset.h
 * @brief ANEM16 instruction set
 * @autor Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#ifndef INSTRSET_H_
#define INSTRSET_H_

#define ANEM_OPCODE_R 	0x0
#define ANEM_OPCODE_S 	0x1
#define ANEM_OPCODE_SW 	0x2
#define ANEM_OPCODE_LW  0x3
#define ANEM_OPCODE_LIU 0x4
#define ANEM_OPCODE_LIL 0x5
#define ANEM_OPCODE_BZ  0x8
//0x9 , 0xA reserved for BZ predication
#define ANEM_OPCODE_JR  0xC
#define ANEM_OPCODE_JAL 0xD
#define ANEM_OPCODE_J   0xF

//0xB, 0xE, 0x7, 0x6 FREE

typedef struct ANEM_I_S
{
	uint8_t opcode;
	uint8_t rega;
	uint8_t regb;

	union
	{
		uint8_t func;
		uint8_t offset;
	};

	uint8_t byte;

	union
	{
		uint16_t address;
		int16_t offset;
	};

} ANEMInstruction;


#endif /* INSTRSET_H_ */
