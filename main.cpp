/***********************
 * @file main.cpp
 * @brief ANEM16sim main file
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"

ANEMCPU cpu(true);

int main(void)
{



	//reset CPU
	cpu.reset();


	//have to figure out how/when to get out of simulation
	while (cpu.programEnd() == false)
	{

		cpu.clockCycle();

	}

	return 0;
}

