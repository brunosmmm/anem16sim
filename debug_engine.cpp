/*
 * @file debug_engine.cpp
 * @brief ANEM16 debug engine — pure-logic debug layer with no I/O
 */

#include "debug_engine.h"
#include "snapshot.h"
#include "disasm.h"
#include "periph/gpio.h"
#include "periph/timer.h"
#include "periph/uart.h"

DebugEngine::DebugEngine(ANEMCPU& cpu, bool traceOn)
	: cpu(cpu), traceEnabled(traceOn)
{
	cpu.getDataMemory().setAccessLog(true);
}

void DebugEngine::notifyStateChange()
{
	for (auto& cb : stateChangeCallbacks)
		cb();
}

void DebugEngine::updateHeat()
{
	// Decay all existing entries
	for (auto it = memHeat.begin(); it != memHeat.end(); ) {
		if (it->second.heat <= 8)
			it = memHeat.erase(it);
		else {
			it->second.heat -= 8;
			++it;
		}
	}
	// Boost recently accessed addresses
	const auto& log = cpu.getDataMemory().getAccessLog();
	for (const auto& a : log) {
		auto& e = memHeat[a.address];
		e.heat = 255;
		e.lastWrite = a.write;
	}
}

void DebugEngine::pushHistory(const m2w& committed)
{
	if (committed.bubble) return;
	HistoryEntry entry;
	entry.pc = committed.pc;
	entry.asmText = ::disassemble(committed.ireg, committed.pc);
	entry.destReg = (committed.reg_ctl != regNOP) ? committed.rega_sel : -1;
	entry.bubble = false;
	history.push_back(entry);
	if (history.size() > HISTORY_SIZE)
		history.erase(history.begin());
}

bool DebugEngine::checkBreakpoints()
{
	// Check against the fetch PC (next instruction to enter pipeline)
	// This matches GDB's model where a breakpoint stops BEFORE execution
	return breakpoints.count(cpu.getPC()) > 0;
}


bool DebugEngine::checkWatchpoints()
{
	if (watchpoints.empty()) return false;
	const auto& log = cpu.getDataMemory().getAccessLog();
	if (log.empty()) return false;
	const auto& last = log.back();
	return watchpoints.count(last.address) > 0;
}

std::optional<MemAccessInfo> DebugEngine::getWatchAccess()
{
	const auto& log = cpu.getDataMemory().getAccessLog();
	if (log.empty()) return std::nullopt;
	const auto& last = log.back();
	return MemAccessInfo{last.address, last.value, last.write};
}

StepResult DebugEngine::step(unsigned int count)
{
	std::lock_guard lk(mtx);
	StepResult result{};
	result.reason = StopReason::None;
	result.halted = false;

	auto done = [&]() { notifyStateChange(); return result; };

	// In debug mode, reset stuck-PC detection so it doesn't accumulate
	// across GDB commands and falsely trigger program termination.
	if (debugMode)
		cpu.resetHaltDetection();

	for (unsigned int i = 0; i < count; i++)
	{
		if (cpu.programEnd())
		{
			halted = true;
			result.halted = true;
			result.reason = StopReason::Halted;
			result.pc = cpu.getPC();
			result.cycle = cpu.getCycleCount();
			return done();
		}

		// Step one instruction: keep clocking until the fetch PC changes.
		// A stalled pipeline can hold the PC constant for multiple cycles;
		// GDB expects "step" to advance to the next instruction.
		addr_t startPC = cpu.getPC();
		static constexpr unsigned int MAX_STALL_CYCLES = 16;
		for (unsigned int s = 0; s < MAX_STALL_CYCLES; s++)
		{
			cpu.getDataMemory().clearAccessLog();
			lastCommitted = cpu.getMemToWB();
			cpu.clockCycle();
			hasCommitted = true;
			pushHistory(lastCommitted);
			updateHeat();
			if (periphClockFn) periphClockFn();

			if (traceEnabled && traceCallback)
			{
				traceCallback(cpu.getCycleCount(), cpu.getPC(),
				              ::disassemble(cpu.getFetchToDecode().ireg, cpu.getFetchToDecode().pc));
			}

			if (cpu.getPC() != startPC)
				break;
		}

		if (i < count - 1 && checkBreakpoints())
		{
			result.reason = StopReason::Breakpoint;
			result.pc = cpu.getPC();
			result.cycle = cpu.getCycleCount();
			result.halted = false;
			return done();
		}

		if (!watchpoints.empty() && checkWatchpoints())
		{
			result.reason = StopReason::Watchpoint;
			result.watchAccess = getWatchAccess();
			result.pc = cpu.getPC();
			result.cycle = cpu.getCycleCount();
			result.halted = false;
			return done();
		}
	}

	result.pc = cpu.getPC();
	result.cycle = cpu.getCycleCount();
	return done();
}

StepResult DebugEngine::runBatch(unsigned int batchSize)
{
	std::lock_guard lk(mtx);
	StepResult result{};
	result.reason = StopReason::None;
	result.halted = false;
	auto done = [&]() { notifyStateChange(); return result; };

	// In debug mode, reset stuck-PC detection so it doesn't accumulate
	// across GDB commands and falsely trigger program termination.
	if (debugMode)
		cpu.resetHaltDetection();

	bool hadLog = !watchpoints.empty();
	if (hadLog)
		cpu.getDataMemory().setAccessLog(true);

	for (unsigned int i = 0; i < batchSize; i++)
	{
		// Check breakpoints BEFORE clocking: catches the case where GDB
		// set the PC to a breakpoint address (e.g., step-by-breakpoint).
		// Skip on the very first iteration if the PC hasn't been changed
		// by GDB (handled by caller via startPC skip).
		if (i == 0 && debugMode && checkBreakpoints())
		{
			result.reason = StopReason::Breakpoint;
			break;
		}

		if (cpu.programEnd())
		{
			halted = true;
			result.halted = true;
			result.reason = StopReason::Halted;
			break;
		}

		cpu.getDataMemory().clearAccessLog();
		lastCommitted = cpu.getMemToWB();
		cpu.clockCycle();
		hasCommitted = true;
		pushHistory(lastCommitted);
		updateHeat();
		if (periphClockFn) periphClockFn();

		if (traceEnabled && traceCallback)
		{
			traceCallback(cpu.getCycleCount(), cpu.getPC(),
			              ::disassemble(cpu.getFetchToDecode().ireg, cpu.getFetchToDecode().pc));
		}

		if (checkBreakpoints())
		{
			result.reason = StopReason::Breakpoint;
			break;
		}

		if (hadLog && checkWatchpoints())
		{
			result.reason = StopReason::Watchpoint;
			result.watchAccess = getWatchAccess();
			break;
		}
	}

	result.pc = cpu.getPC();
	result.cycle = cpu.getCycleCount();
	if (result.reason == StopReason::None && cpu.programEnd())
	{
		halted = true;
		result.halted = true;
		result.reason = StopReason::Halted;
	}
	return done();
}

// Breakpoints

void DebugEngine::addBreakpoint(addr_t addr)
{
	std::lock_guard lk(mtx);
	breakpoints.insert(addr);
}

bool DebugEngine::removeBreakpoint(addr_t addr)
{
	std::lock_guard lk(mtx);
	return breakpoints.erase(addr) > 0;
}

std::vector<addr_t> DebugEngine::listBreakpoints() const
{
	std::lock_guard lk(mtx);
	return {breakpoints.begin(), breakpoints.end()};
}

// Watchpoints

void DebugEngine::addWatchpoint(addr_t addr)
{
	std::lock_guard lk(mtx);
	watchpoints.insert(addr);
	cpu.getDataMemory().setAccessLog(true);
}

bool DebugEngine::removeWatchpoint(addr_t addr)
{
	std::lock_guard lk(mtx);
	bool removed = watchpoints.erase(addr) > 0;
	if (watchpoints.empty())
		cpu.getDataMemory().setAccessLog(false);
	return removed;
}

std::vector<addr_t> DebugEngine::listWatchpoints() const
{
	std::lock_guard lk(mtx);
	return {watchpoints.begin(), watchpoints.end()};
}

// Inspection

RegistersResult DebugEngine::getRegisters() const
{
	std::lock_guard lk(mtx);
	RegistersResult r{};
	r.pc = cpu.getPC();
	for (int i = 0; i < 16; i++)
		r.gpr[i] = cpu.readRegister(i);
	r.hi = cpu.getHI();
	r.lo = cpu.getLO();
	r.sp = cpu.getSP();
	r.epc = cpu.getEPC();
	r.eca = cpu.getECA();
	r.ien = cpu.getIEN();
	r.zFlag = cpu.getZFlag();
	return r;
}

data_t DebugEngine::getRegister(uint8_t n) const
{
	std::lock_guard lk(mtx);
	return cpu.readRegister(n);
}

std::vector<MemoryEntry> DebugEngine::getMemory(addr_t start, addr_t count) const
{
	std::lock_guard lk(mtx);
	std::vector<MemoryEntry> entries;
	entries.reserve(count);
	for (addr_t i = start; i < start + count; i++) {
		auto it = memHeat.find(i);
		uint8_t heat = (it != memHeat.end()) ? it->second.heat : 0;
		bool lw = (it != memHeat.end()) ? it->second.lastWrite : false;
		entries.push_back({i, cpu.readDataMem(i), heat, lw});
	}
	return entries;
}

std::vector<DisasmEntry> DebugEngine::disassemble(addr_t start, addr_t count) const
{
	std::lock_guard lk(mtx);
	std::vector<DisasmEntry> entries;
	entries.reserve(count);
	for (addr_t i = start; i < start + count; i++)
	{
		ANEMInstruction instr = cpu.readInstrMem(i);
		auto srcLoc = ::lookupSourceLoc(i);
		entries.push_back({i, ::disassemble(instr, i), instr.toWord(), i == cpu.getPC(),
		                   ::lookupSymbol(i), srcLoc.file, srcLoc.line});
	}
	return entries;
}

PipelineResult DebugEngine::getPipeline() const
{
	std::lock_guard lk(mtx);
	auto& f = cpu.getFetchToDecode();
	auto& d = cpu.getDecodeToExec();
	auto& e = cpu.getExecToMem();
	auto& m = cpu.getMemToWB();

	PipelineResult r{};
	r.cycle = cpu.getCycleCount();

	r.ifStage = {f.pc, f.bubble ? "<bubble>" : ::disassemble(f.ireg, f.pc), f.bubble, -1};

	r.idStage.pc = d.pc;
	r.idStage.asmText = d.bubble ? "<bubble>" : ::disassemble(d.ireg, d.pc);
	r.idStage.bubble = d.bubble;
	r.idStage.destReg = (!d.bubble && d.reg_ctl != regNOP) ? d.rega_sel : -1;
	r.idStage.fwdAluAlu = d.fwd_alu_alua || d.fwd_alu_alub;
	r.idStage.fwdMemAlu = d.fwd_mem_alua || d.fwd_mem_alub;

	r.exStage = {e.pc, e.bubble ? "<bubble>" : ::disassemble(e.ireg, e.pc), e.bubble,
	             (!e.bubble && e.reg_ctl != regNOP) ? (int)e.rega_sel : -1};
	r.memStage = {m.pc, m.bubble ? "<bubble>" : ::disassemble(m.ireg, m.pc), m.bubble,
	              (!m.bubble && m.reg_ctl != regNOP) ? (int)m.rega_sel : -1};

	r.hasCommitted = hasCommitted;
	if (hasCommitted && !lastCommitted.bubble) {
		r.wbStage = {lastCommitted.pc, ::disassemble(lastCommitted.ireg, lastCommitted.pc), false,
		             (lastCommitted.reg_ctl != regNOP) ? (int)lastCommitted.rega_sel : -1};
	} else {
		r.wbStage = {0, "<bubble>", true, -1};
	}

	r.stalled = cpu.isStalled();
	r.zFlag = cpu.getZFlag();

	return r;
}

StatsResult DebugEngine::getStats() const
{
	std::lock_guard lk(mtx);
	const auto& c = cpu.getCounters();
	return {
		c.getCycleCount(),
		c.getCycleCount(),  // instructions decoded tracked same as cycles in counters
		c.getStallCount(),
		c.getBubbleCount(),
		c.getForwardAluAluCount(),
		c.getForwardMemAluCount()
	};
}

void DebugEngine::dumpFullStats(std::ostream& out) const
{
	std::lock_guard lk(mtx);
	cpu.dumpStats(out);
}

StatusResult DebugEngine::getStatus() const
{
	std::lock_guard lk(mtx);
	return {false, halted, cpu.getPC(), cpu.getCycleCount()};
}

// Register write

void DebugEngine::writeRegister(uint8_t reg, data_t value)
{
	std::lock_guard lk(mtx);
	cpu.writeRegister(reg, value);
	notifyStateChange();
}

void DebugEngine::setPC(addr_t pc)
{
	std::lock_guard lk(mtx);
	cpu.setPC(pc);
	notifyStateChange();
}

// Memory write

void DebugEngine::writeMemory(addr_t addr, data_t value)
{
	std::lock_guard lk(mtx);
	cpu.getDataMemory().write(addr, value);
	notifyStateChange();
}

// Interrupt control

void DebugEngine::assertInterrupt()
{
	std::lock_guard lk(mtx);
	cpu.setIntPin(true);
}

void DebugEngine::deassertInterrupt()
{
	std::lock_guard lk(mtx);
	cpu.setIntPin(false);
}

// Control

void DebugEngine::setTrace(bool enabled)
{
	std::lock_guard lk(mtx);
	traceEnabled = enabled;
}

// Snapshots

void DebugEngine::saveSnapshot(const std::string& path, const std::string& programFile)
{
	std::lock_guard lk(mtx);
	snapshot::saveToFile(cpu, path, programFile);
}

void DebugEngine::loadSnapshot(const std::string& path)
{
	std::lock_guard lk(mtx);
	snapshot::loadFromFile(cpu, path);
	halted = false;
	notifyStateChange();
}

void DebugEngine::reset()
{
	std::lock_guard lk(mtx);
	cpu.reset();
	if (gpioPtr) gpioPtr->reset();
	if (timerPtr) timerPtr->reset();
	if (uartPtr) uartPtr->reset();
	halted = false;
	hasCommitted = false;
	lastCommitted = {};
	history.clear();
	memHeat.clear();
	notifyStateChange();
}
