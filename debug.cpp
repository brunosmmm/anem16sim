/*
 * @file debug.cpp
 * @brief ANEM16 interactive debugger (REPL frontend)
 */

#include "debug.h"
#include "disasm.h"
#include <iostream>
#include <iomanip>
#include <sstream>

ANEMDebugger::ANEMDebugger(ANEMCPU& cpu, bool traceOn)
	: engine(cpu, traceOn)
{
	// Set trace callback to print to stdout
	engine.setTraceCallback([](unsigned long long cycle, addr_t pc,
	                           const std::string& asmText) {
		std::cout << "[" << std::setw(6) << cycle << "] "
		          << "PC=0x" << std::hex << std::setfill('0') << std::setw(4) << pc
		          << " " << std::setfill(' ') << std::dec
		          << asmText << std::endl;
	});
}

std::vector<std::string> ANEMDebugger::tokenize(const std::string& line) const
{
	std::vector<std::string> tokens;
	std::istringstream iss(line);
	std::string tok;
	while (iss >> tok)
		tokens.push_back(tok);
	return tokens;
}

addr_t ANEMDebugger::parseAddr(const std::string& s) const
{
	if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		return (addr_t)std::stoul(s, nullptr, 16);
	return (addr_t)std::stoul(s, nullptr, 10);
}

void ANEMDebugger::cmdStep(unsigned int n)
{
	auto result = engine.step(n);

	switch (result.reason)
	{
	case StopReason::Halted:
		std::cout << "Program halted." << std::endl;
		break;
	case StopReason::Breakpoint:
		std::cout << "Breakpoint hit at PC=0x" << std::hex << std::setw(4)
		          << std::setfill('0') << result.pc << std::dec
		          << std::setfill(' ') << std::endl;
		break;
	case StopReason::Watchpoint:
		if (result.watchAccess)
		{
			auto& wa = *result.watchAccess;
			std::cout << "Watchpoint hit: " << (wa.write ? "write" : "read")
			          << " at addr=0x" << std::hex << std::setw(4)
			          << std::setfill('0') << wa.address
			          << " val=0x" << std::setw(4) << wa.value
			          << std::dec << std::setfill(' ') << std::endl;
		}
		break;
	case StopReason::None:
		break;
	}
}

void ANEMDebugger::cmdContinue()
{
	// Run in a tight loop using runBatch with large batch size
	while (true)
	{
		auto result = engine.runBatch(10000);

		if (result.reason == StopReason::Breakpoint)
		{
			std::cout << "Breakpoint hit at PC=0x" << std::hex << std::setw(4)
			          << std::setfill('0') << result.pc << std::dec
			          << std::setfill(' ') << std::endl;
			return;
		}

		if (result.reason == StopReason::Watchpoint)
		{
			if (result.watchAccess)
			{
				auto& wa = *result.watchAccess;
				std::cout << "Watchpoint hit: " << (wa.write ? "write" : "read")
				          << " at addr=0x" << std::hex << std::setw(4)
				          << std::setfill('0') << wa.address
				          << " val=0x" << std::setw(4) << wa.value
				          << std::dec << std::setfill(' ') << std::endl;
			}
			return;
		}

		if (result.reason == StopReason::Halted)
		{
			std::cout << "Program halted." << std::endl;
			return;
		}
	}
}

void ANEMDebugger::cmdBreakpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		auto bps = engine.listBreakpoints();
		if (bps.empty())
		{
			std::cout << "No breakpoints set." << std::endl;
			return;
		}
		std::cout << "Breakpoints:" << std::endl;
		for (addr_t bp : bps)
			std::cout << "  0x" << std::hex << std::setfill('0')
			          << std::setw(4) << bp << std::dec
			          << std::setfill(' ') << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	engine.addBreakpoint(addr);
	std::cout << "Breakpoint set at 0x" << std::hex << std::setfill('0')
	          << std::setw(4) << addr << std::dec
	          << std::setfill(' ') << std::endl;
}

void ANEMDebugger::cmdDeleteBreakpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: db <addr>" << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	if (engine.removeBreakpoint(addr))
		std::cout << "Breakpoint deleted." << std::endl;
	else
		std::cout << "No breakpoint at that address." << std::endl;
}

void ANEMDebugger::cmdWatchpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		auto wps = engine.listWatchpoints();
		if (wps.empty())
		{
			std::cout << "No watchpoints set." << std::endl;
			return;
		}
		std::cout << "Watchpoints:" << std::endl;
		for (addr_t wp : wps)
			std::cout << "  0x" << std::hex << std::setfill('0')
			          << std::setw(4) << wp << std::dec
			          << std::setfill(' ') << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	engine.addWatchpoint(addr);
	std::cout << "Watchpoint set at 0x" << std::hex << std::setfill('0')
	          << std::setw(4) << addr << std::dec
	          << std::setfill(' ') << std::endl;
}

void ANEMDebugger::cmdDeleteWatchpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: dw <addr>" << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	if (engine.removeWatchpoint(addr))
	{
		std::cout << "Watchpoint deleted." << std::endl;
	}
	else
		std::cout << "No watchpoint at that address." << std::endl;
}

void ANEMDebugger::cmdRegisters(const std::vector<std::string>& args)
{
	if (args.size() >= 2)
	{
		uint8_t reg = (uint8_t)std::stoul(args[1]);
		if (reg >= 16)
		{
			std::cout << "Register index must be 0-15." << std::endl;
			return;
		}
		std::cout << regName(reg) << " = 0x" << std::hex << std::setfill('0')
		          << std::setw(4) << engine.getRegister(reg) << std::dec
		          << std::setfill(' ') << std::endl;
		return;
	}

	auto regs = engine.getRegisters();
	std::cout << "PC  = 0x" << std::hex << std::setfill('0') << std::setw(4) << regs.pc << std::endl;
	for (int i = 0; i < 16; i++)
	{
		std::cout << std::left << std::setw(4) << std::setfill(' ') << regName(i)
		          << std::right << "= 0x" << std::hex << std::setfill('0') << std::setw(4)
		          << regs.gpr[i] << std::setfill(' ') << std::dec;
		if ((i % 4) == 3)
			std::cout << std::endl;
		else
			std::cout << "  ";
	}
	std::cout << std::right << "HI  = 0x" << std::hex << std::setfill('0') << std::setw(4) << regs.hi
	          << "  LO  = 0x" << std::setw(4) << regs.lo << std::dec
	          << std::setfill(' ') << std::endl;
}

void ANEMDebugger::cmdMemory(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: m <start> [count]" << std::endl;
		return;
	}
	addr_t start = parseAddr(args[1]);
	addr_t count = (args.size() >= 3) ? (addr_t)std::stoul(args[2]) : 16;

	auto entries = engine.getMemory(start, count);
	for (auto& e : entries)
	{
		std::cout << "  [0x" << std::hex << std::setfill('0') << std::setw(4) << e.address
		          << "] = 0x" << std::setw(4) << e.value
		          << std::dec << std::setfill(' ') << std::endl;
	}
}

void ANEMDebugger::cmdDisassemble(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: d <start> [count]" << std::endl;
		return;
	}
	addr_t start = parseAddr(args[1]);
	addr_t count = (args.size() >= 3) ? (addr_t)std::stoul(args[2]) : 16;

	auto entries = engine.disassemble(start, count);
	for (auto& e : entries)
	{
		std::string marker = e.current ? " >> " : "    ";
		std::cout << marker << "[0x" << std::hex << std::setfill('0') << std::setw(4) << e.address
		          << "] " << std::setfill(' ') << std::dec
		          << e.asmText << std::endl;
	}
}

void ANEMDebugger::cmdPipeline()
{
	auto p = engine.getPipeline();

	std::cout << "=== Pipeline (cycle " << p.cycle << ") ===" << std::endl;

	std::cout << "  IF:  [0x" << std::hex << std::setfill('0') << std::setw(4) << p.ifStage.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (p.ifStage.bubble) std::cout << "<bubble>";
	else std::cout << p.ifStage.asmText;
	std::cout << std::endl;

	std::cout << "  ID:  [0x" << std::hex << std::setfill('0') << std::setw(4) << p.idStage.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (p.idStage.bubble) std::cout << "<bubble>";
	else std::cout << p.idStage.asmText;
	if (p.idStage.fwdAluAlu)
		std::cout << "  [fwd: ALU->ALU]";
	if (p.idStage.fwdMemAlu)
		std::cout << "  [fwd: MEM->ALU]";
	std::cout << std::endl;

	std::cout << "  EX:  [0x" << std::hex << std::setfill('0') << std::setw(4) << p.exStage.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (p.exStage.bubble) std::cout << "<bubble>";
	else std::cout << p.exStage.asmText;
	std::cout << std::endl;

	std::cout << "  MEM: [0x" << std::hex << std::setfill('0') << std::setw(4) << p.memStage.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (p.memStage.bubble) std::cout << "<bubble>";
	else std::cout << p.memStage.asmText;
	std::cout << std::endl;

	std::cout << "  Stall: " << (p.stalled ? "yes" : "none") << std::endl;
}

void ANEMDebugger::cmdTrace(const std::vector<std::string>& args)
{
	if (args.size() >= 2)
	{
		if (args[1] == "on") engine.setTrace(true);
		else if (args[1] == "off") engine.setTrace(false);
		else std::cout << "Usage: t [on|off]" << std::endl;
	}
	else
	{
		engine.setTrace(!engine.getTrace());
	}
	std::cout << "Trace " << (engine.getTrace() ? "enabled" : "disabled") << std::endl;
}

void ANEMDebugger::cmdStats()
{
	engine.dumpFullStats(std::cout);
}

void ANEMDebugger::cmdSave(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: save <file>" << std::endl;
		return;
	}
	try
	{
		engine.saveSnapshot(args[1]);
		std::cout << "Snapshot saved to " << args[1] << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cout << "Error saving snapshot: " << e.what() << std::endl;
	}
}

void ANEMDebugger::cmdLoad(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: load <file>" << std::endl;
		return;
	}
	try
	{
		engine.loadSnapshot(args[1]);
		std::cout << "Snapshot loaded from " << args[1] << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cout << "Error loading snapshot: " << e.what() << std::endl;
	}
}

void ANEMDebugger::cmdReset()
{
	engine.reset();
	std::cout << "CPU reset." << std::endl;
}

void ANEMDebugger::cmdHelp()
{
	std::cout << "Commands:" << std::endl;
	std::cout << "  s [N]          Step N cycles (default 1)" << std::endl;
	std::cout << "  c              Continue until breakpoint/halt" << std::endl;
	std::cout << "  b [addr]       Set breakpoint / list breakpoints" << std::endl;
	std::cout << "  db <addr>      Delete breakpoint" << std::endl;
	std::cout << "  w [addr]       Set watchpoint / list watchpoints" << std::endl;
	std::cout << "  dw <addr>      Delete watchpoint" << std::endl;
	std::cout << "  r [N]          Show registers / show register N" << std::endl;
	std::cout << "  m <start> [N]  Dump data memory" << std::endl;
	std::cout << "  d <start> [N]  Disassemble instruction memory" << std::endl;
	std::cout << "  p              Show pipeline state" << std::endl;
	std::cout << "  t [on|off]     Toggle execution trace" << std::endl;
	std::cout << "  stats          Show statistics" << std::endl;
	std::cout << "  save <file>    Save state snapshot (JSON)" << std::endl;
	std::cout << "  load <file>    Load state snapshot (JSON)" << std::endl;
	std::cout << "  reset          Reset CPU" << std::endl;
	std::cout << "  q              Quit" << std::endl;
	std::cout << "Addresses can be decimal or hex (0x prefix)." << std::endl;
}

void ANEMDebugger::run()
{
	std::string line;

	std::cout << "ANEM16 Debugger. Type 'h' for help." << std::endl;
	std::cout << "Program loaded, PC=0x" << std::hex << std::setfill('0')
	          << std::setw(4) << engine.getRegisters().pc << std::dec
	          << std::setfill(' ') << std::endl;

	while (true)
	{
		std::cout << "anem> " << std::flush;
		if (!std::getline(std::cin, line))
			break;

		auto tokens = tokenize(line);
		if (tokens.empty()) continue;

		const std::string& cmd = tokens[0];

		try
		{
			if (cmd == "q" || cmd == "quit")
				break;
			else if (cmd == "h" || cmd == "help")
				cmdHelp();
			else if (cmd == "s" || cmd == "step")
			{
				unsigned int n = 1;
				if (tokens.size() >= 2) n = std::stoul(tokens[1]);
				cmdStep(n);
			}
			else if (cmd == "c" || cmd == "continue")
				cmdContinue();
			else if (cmd == "b")
				cmdBreakpoint(tokens);
			else if (cmd == "db")
				cmdDeleteBreakpoint(tokens);
			else if (cmd == "w")
				cmdWatchpoint(tokens);
			else if (cmd == "dw")
				cmdDeleteWatchpoint(tokens);
			else if (cmd == "r" || cmd == "reg")
				cmdRegisters(tokens);
			else if (cmd == "m" || cmd == "mem")
				cmdMemory(tokens);
			else if (cmd == "d" || cmd == "dis")
				cmdDisassemble(tokens);
			else if (cmd == "p" || cmd == "pipe")
				cmdPipeline();
			else if (cmd == "t" || cmd == "trace")
				cmdTrace(tokens);
			else if (cmd == "stats")
				cmdStats();
			else if (cmd == "save")
				cmdSave(tokens);
			else if (cmd == "load")
				cmdLoad(tokens);
			else if (cmd == "reset")
				cmdReset();
			else
				std::cout << "Unknown command: " << cmd << ". Type 'h' for help." << std::endl;
		}
		catch (const std::exception& ex)
		{
			std::cout << "Error: " << ex.what() << std::endl;
		}
	}
}
