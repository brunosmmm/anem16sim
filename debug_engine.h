/*
 * @file debug_engine.h
 * @brief ANEM16 debug engine — pure-logic debug layer with no I/O
 */

#ifndef DEBUG_ENGINE_H_
#define DEBUG_ENGINE_H_

#include "cpu.h"
#include "instrset.h"
#include "snapshot.h"
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <iosfwd>
#include <optional>
#include <unordered_map>
#include <mutex>

class ANEMPeripheralGPIO;
class ANEMPeripheralTimer;
class ANEMPeripheralUART;

enum class StopReason { None, Breakpoint, Watchpoint, Halted };

struct MemAccessInfo {
	addr_t address;
	data_t value;
	bool write;
};

struct StepResult {
	addr_t pc;
	unsigned long long cycle;
	bool halted;
	StopReason reason;
	std::optional<MemAccessInfo> watchAccess;
};

struct RegistersResult {
	addr_t pc;
	data_t gpr[16];
	data_t hi;
	data_t lo;
	data_t sp;
	data_t epc;
	data_t eca;
	bool ien;
	bool zFlag;
};

struct MemoryEntry {
	addr_t address;
	data_t value;
	uint8_t heat;       // 0=cold, 255=hot (recent access)
	bool lastWrite;     // true if last access was a write
};

struct DisasmEntry {
	addr_t address;
	std::string asmText;
	uint16_t word;
	bool current;  // PC points here
	std::string symbol; // label at this address (empty if none)
	std::string srcFile;
	int srcLine = 0;
};

struct PipelineStage {
	addr_t pc;
	std::string asmText;
	bool bubble;
	int destReg = -1;  // register being written (-1 = none)
	// Forwarding info (ID stage only)
	bool fwdAluAlu = false;
	bool fwdMemAlu = false;
};

struct HistoryEntry {
	addr_t pc;
	std::string asmText;
	int destReg;  // -1 = no write
	bool bubble;
};

struct PipelineResult {
	unsigned long long cycle;
	PipelineStage ifStage;
	PipelineStage idStage;
	PipelineStage exStage;
	PipelineStage memStage;
	PipelineStage wbStage;   // last committed instruction
	bool hasCommitted;       // true once pipeline has produced at least one commit
	bool stalled;
	bool zFlag;
};

struct StatsResult {
	long long cycles;
	long long instructions;
	long long stalls;
	long long bubbles;
	long long fwdAluAlu;
	long long fwdMemAlu;
};

struct StatusResult {
	bool running;
	bool halted;
	addr_t pc;
	unsigned long long cycle;
};

using EventCallback = std::function<void(StopReason, addr_t pc,
                                          unsigned long long cycle,
                                          std::optional<MemAccessInfo>)>;
using TraceEventCallback = std::function<void(unsigned long long cycle,
                                               addr_t pc,
                                               const std::string& asmText)>;

class DebugEngine
{
public:
	using StateChangeCallback = std::function<void()>;
private:
	ANEMCPU& cpu;
	std::set<addr_t> breakpoints;
	std::set<addr_t> watchpoints;
	bool traceEnabled = false;
	bool halted = false;
	bool debugMode = false; // GDB attached — skip programEnd() checks

	// Last committed instruction (saved from mem_to_wb before each clockCycle)
	m2w lastCommitted{};
	bool hasCommitted = false;

	// Execution history ring buffer
	static constexpr size_t HISTORY_SIZE = 32;
	std::vector<HistoryEntry> history;
	void pushHistory(const m2w& committed);

	// Memory heatmap: tracks recent access intensity per address
	struct HeatEntry { uint8_t heat; bool lastWrite; };
	std::unordered_map<addr_t, HeatEntry> memHeat;
	void updateHeat();

	EventCallback eventCallback;
	TraceEventCallback traceCallback;
	std::vector<StateChangeCallback> stateChangeCallbacks;
	void notifyStateChange();

	std::function<void()> periphClockFn;
	ANEMPeripheralGPIO* gpioPtr = nullptr;
	ANEMPeripheralTimer* timerPtr = nullptr;
	ANEMPeripheralUART* uartPtr = nullptr;

	bool checkBreakpoints();
	bool checkWatchpoints();
	std::optional<MemAccessInfo> getWatchAccess();

	mutable std::recursive_mutex mtx;

public:
	std::unique_lock<std::recursive_mutex> lock() const { return std::unique_lock(mtx); }

	DebugEngine(ANEMCPU& cpu, bool traceOn = false);

	// Execution
	void setDebugMode(bool enabled) { debugMode = enabled; }
	StepResult step(unsigned int count = 1);
	StepResult runBatch(unsigned int batchSize);

	// Breakpoints
	void addBreakpoint(addr_t addr);
	bool removeBreakpoint(addr_t addr);
	std::vector<addr_t> listBreakpoints() const;

	// Watchpoints
	void addWatchpoint(addr_t addr);
	bool removeWatchpoint(addr_t addr);
	std::vector<addr_t> listWatchpoints() const;

	// Inspection
	RegistersResult getRegisters() const;
	data_t getRegister(uint8_t n) const;
	std::vector<MemoryEntry> getMemory(addr_t start, addr_t count) const;
	std::vector<DisasmEntry> disassemble(addr_t start, addr_t count) const;
	PipelineResult getPipeline() const;
	const std::vector<HistoryEntry>& getHistory() const { return history; }
	StatsResult getStats() const;
	void dumpFullStats(std::ostream& out) const;
	StatusResult getStatus() const;

	// Memory write
	void writeMemory(addr_t addr, data_t value);

	// Register write
	void writeRegister(uint8_t reg, data_t value);
	void setPC(addr_t pc);

	// Snapshots
	void saveSnapshot(const std::string& path, const std::string& programFile = "");
	void loadSnapshot(const std::string& path);

	// Interrupt control
	void assertInterrupt();
	void deassertInterrupt();

	// Control
	void setTrace(bool enabled);
	bool getTrace() const { return traceEnabled; }
	void reset();
	bool isHalted() const { return halted; }

	// Callbacks
	void setEventCallback(EventCallback cb) { eventCallback = std::move(cb); }
	void setTraceCallback(TraceEventCallback cb) { traceCallback = std::move(cb); }
	void addStateChangeCallback(StateChangeCallback cb) { std::lock_guard lk(mtx); stateChangeCallbacks.push_back(std::move(cb)); }

	// Peripheral support
	void setPeripheralClock(std::function<void()> fn) { periphClockFn = std::move(fn); }
	void setGPIO(ANEMPeripheralGPIO* p) { gpioPtr = p; }
	void setTimer(ANEMPeripheralTimer* p) { timerPtr = p; }
	void setUART(ANEMPeripheralUART* p) { uartPtr = p; }
	ANEMPeripheralGPIO* getGPIO() const { return gpioPtr; }
	ANEMPeripheralTimer* getTimer() const { return timerPtr; }
	ANEMPeripheralUART* getUART() const { return uartPtr; }
};

#endif /* DEBUG_ENGINE_H_ */
