/***********************
 * @file main.cpp
 * @brief ANEM16sim main file
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "cpu.h"
#include "debug.h"
#include "debug_server.h"
#include "disasm.h"
#include "except.h"
#include "trace.h"
#include "snapshot.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include "periph/mac.h"

static void printUsage(const char* prog)
{
	std::cerr << "Usage: " << prog << " [options] <program>" << std::endl;
	std::cerr << "  -d          Interactive debug mode" << std::endl;
	std::cerr << "  -r [port]   Remote debug server (JSON-RPC over ZMQ, default port 6808)" << std::endl;
	std::cerr << "  -t          Enable execution trace" << std::endl;
	std::cerr << "  -T <file>   Write HW-compatible trace to file" << std::endl;
	std::cerr << "  -o <file>   Save JSON state snapshot on completion" << std::endl;
	std::cerr << "  -s          Print statistics on exit" << std::endl;
	std::cerr << "  -m N        Set max cycles (default 10000)" << std::endl;
	std::cerr << "  -h          Show help" << std::endl;
}

int main(int argc, char *argv[])
{
	bool debugMode = false;
	bool remoteMode = false;
	int remotePort = 6808;
	bool traceMode = false;
	bool statsMode = false;
	unsigned int maxCycles = 10000;
	const char* progFile = nullptr;
	const char* hwTraceFile = nullptr;
	const char* snapshotFile = nullptr;

	// Parse arguments
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-d") == 0)
			debugMode = true;
		else if (strcmp(argv[i], "-r") == 0)
		{
			remoteMode = true;
			// Optional port argument (next arg if it looks like a number)
			if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				try {
					remotePort = std::stoi(argv[i + 1]);
					i++;
				} catch (...) {
					// Not a number, use default port
				}
			}
		}
		else if (strcmp(argv[i], "-t") == 0)
			traceMode = true;
		else if (strcmp(argv[i], "-s") == 0)
			statsMode = true;
		else if (strcmp(argv[i], "-T") == 0)
		{
			if (i + 1 < argc)
				hwTraceFile = argv[++i];
			else
			{
				std::cerr << "-T requires a file argument" << std::endl;
				return 1;
			}
		}
		else if (strcmp(argv[i], "-o") == 0)
		{
			if (i + 1 < argc)
				snapshotFile = argv[++i];
			else
			{
				std::cerr << "-o requires a file argument" << std::endl;
				return 1;
			}
		}
		else if (strcmp(argv[i], "-m") == 0)
		{
			if (i + 1 < argc)
				maxCycles = std::stoul(argv[++i]);
			else
			{
				std::cerr << "-m requires an argument" << std::endl;
				return 1;
			}
		}
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
		{
			printUsage(argv[0]);
			return 0;
		}
		else if (argv[i][0] == '-')
		{
			std::cerr << "Unknown option: " << argv[i] << std::endl;
			printUsage(argv[0]);
			return 1;
		}
		else
		{
			progFile = argv[i];
		}
	}

	if (!progFile)
	{
		std::cerr << "Error: no program file specified." << std::endl;
		printUsage(argv[0]);
		return 1;
	}

	ANEMCPU cpu(true);

	//standard peripherals
	ANEMPeripheralMAC mac;

	//attach standard peripherals
	cpu.attachPeripheral(MAC_BASE_ADDR, &mac);

	//reset CPU & peripherals
	cpu.reset();
	mac.reset();

	cpu.setMaxCycles(maxCycles);

	try
	{
		cpu.loadProgram(progFile);
	}
	catch (ANEMProgramLoadException &e)
	{
		std::cout << "Could not load file: " << progFile << std::endl;
		return 1;
	}

	// Set up HW trace writer if requested
	TraceWriter traceWriter;
	if (hwTraceFile)
	{
		if (!traceWriter.open(hwTraceFile))
		{
			std::cerr << "Could not open trace file: " << hwTraceFile << std::endl;
			return 1;
		}
		cpu.setTraceWriter(&traceWriter);
	}

	if (remoteMode)
	{
		ANEMDebugServer server(cpu, remotePort);
		server.run();
	}
	else if (debugMode)
	{
		ANEMDebugger dbg(cpu, traceMode);
		dbg.run();
	}
	else
	{
		// Set up trace callback if requested
		if (traceMode)
		{
			cpu.setTraceCallback([](unsigned long long cycle, addr_t pc,
			                        const ANEMInstruction& instr, const ANEMCPU&) {
				std::cout << "[" << std::setw(6) << cycle << "] "
				          << "PC=0x" << std::hex << std::setfill('0')
				          << std::setw(4) << pc << " "
				          << std::setfill(' ') << std::dec
				          << disassemble(instr) << std::endl;
			});
		}

		// Run simulation
		while (cpu.programEnd() == false)
		{
			cpu.clockCycle();
			mac.clockCycle();
		}

		// Dump final state
		cpu.dumpRegisters();
		cpu.dumpMemory(0, 16);
	}

	// Finish HW trace (writes RF/SR/END)
	if (traceWriter.isOpen())
	{
		traceWriter.finish(cpu);
		traceWriter.close();
	}

	// Save JSON snapshot if requested
	if (snapshotFile)
	{
		snapshot::saveToFile(cpu, snapshotFile, progFile ? progFile : "");
		std::cerr << "Snapshot saved to " << snapshotFile << std::endl;
	}

	if (statsMode)
	{
		std::cout << std::endl;
		cpu.dumpStats(std::cout);
	}

	return 0;
}
