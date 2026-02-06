/***********************
 * @file main.cpp
 * @brief ANEM16sim main file
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"
#include "except.h"
#include <iostream>
#include "periph/mac.h"

ANEMCPU cpu(true);

int main(int argc, char *argv[])
{
	//standard peripherals
	ANEMPeripheralMAC mac;

	//attach standard peripherals
	cpu.attachPeripheral(MAC_BASE_ADDR, &mac);

	//reset CPU & peripherals
	cpu.reset();
	mac.reset();

	std::string progfile = (argc > 1) ? argv[1] : "serie.bin";

	try
	{
	cpu.loadProgram(progfile);
	}
	catch (ANEMProgramLoadException &e)
	{

		//couldn't load file, quit
		std::cout << "Could not load file: " << progfile << std::endl;
		exit(1);

	}

	//have to figure out how/when to get out of simulation
	while (cpu.programEnd() == false)
	{

		cpu.clockCycle();
		mac.clockCycle();

	}

	// Dump final state
	cpu.dumpRegisters();
	cpu.dumpMemory(0, 16);

	return 0;
}

