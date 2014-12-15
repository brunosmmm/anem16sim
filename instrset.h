/*
 * @file instrset.h
 * @brief ANEM16 instruction set
 * @autor Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#ifndef INSTRSET_H_
#define INSTRSET_H_

#include <cstdint>
#include <cstring>

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

//alu functions
#define ANEM_FUNC_ADD 0x2
#define ANEM_FUNC_SUB 0x6
#define ANEM_FUNC_OR  0x1
#define ANEM_FUNC_AND 0x0
#define ANEM_FUNC_XOR 0xF
#define ANEM_FUNC_NOR 0xC
#define ANEM_FUNC_SLT 0x7
#define ANEM_FUNC_SHL 0x2
#define ANEM_FUNC_SHR 0x1
#define ANEM_FUNC_SAR 0x0
#define ANEM_FUNC_ROL 0x8
#define ANEM_FUNC_ROR 0x4

typedef struct ANEM_I_S
{
	uint8_t opcode : 4;

	union
	{

		struct {
			uint8_t rega : 4;

			union
			{

				uint8_t byte;

				struct {

					union
					{
						uint8_t regb : 4;
						uint8_t shamt : 4;
					};

					union
					{
						uint8_t func : 4;
						uint8_t off_4 : 4;
					};
				};

			};
		};

		uint16_t address : 12;
	};

	ANEM_I_S() { memset(this,0,sizeof(ANEM_I_S)); this->opcode = ANEM_OPCODE_R; this->func = ANEM_FUNC_OR; }
	ANEM_I_S(uint16_t iword) { memcpy(this,&iword,sizeof(ANEM_I_S)); }

} ANEMInstruction;

extern ANEMInstruction ANEM_INSTRUCTION_NOP;


#endif /* INSTRSET_H_ */
