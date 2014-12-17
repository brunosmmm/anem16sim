/***********************
 * @file main.cpp
 * @brief ANEM16sim main file
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"
#include "except.h"
#include <iostream>

ANEMCPU cpu(true);

int main(void)
{

	//reset CPU
	cpu.reset();

	try
	{
	cpu.loadProgram("serie.bin");
	}
	catch (ANEMProgramLoadException &e)
	{

		//couldn't load file, quit
		std::cout << "Could not load file!" << std::endl;
		exit(1);

	}

	//have to figure out how/when to get out of simulation
	while (cpu.programEnd() == false)
	{

		cpu.clockCycle();

	}

	return 0;
}

