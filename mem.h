/****
 * @file mem.h
 * @brief instruction and data memory
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#ifndef MEM_H_
#define MEM_H_

#include <cstdint>
#include "instrset.h"
#include <fstream>
#include "periph.h"
#include "types.h"
#include <map>
#include <vector>
#include <string>

typedef struct ANEM_ADR_RANGE
{

	addr_t start;
	addr_t end;

	ANEM_ADR_RANGE(addr_t start, addr_t end) {this->start = start; this->end = end; }

} ANEMMemoryAddressRange;

struct MemAccess {
	unsigned long long cycle;
	addr_t address;
	data_t value;
	bool write;  // true=write, false=read
};

///Data memory for ANEM
class ANEMDataMemory
{
private:
	dmem_t * dmem;
	uint32_t size;
	std::map<addr_t, ANEMMemMappedPeripheral*> vmem;

	// Access logging
	bool accessLogEnabled = false;
	std::vector<MemAccess> accessLog;
	unsigned long long* cyclePtr = nullptr;

public:
	ANEMDataMemory() {dmem = nullptr; size = 0;};
	ANEMDataMemory(uint32_t size);
	uint16_t read(addr_t address);
	void write(addr_t address, dmem_t data);

	void clearMem(void);

	bool attachPeripheral(addr_t address, ANEMMemMappedPeripheral *p);

	// Access logging
	void setAccessLog(bool enable) { accessLogEnabled = enable; }
	void setCyclePtr(unsigned long long* ptr) { cyclePtr = ptr; }
	const std::vector<MemAccess>& getAccessLog() const { return accessLog; }
	void clearAccessLog() { accessLog.clear(); }

	// Peripheral enumeration
	std::vector<std::pair<addr_t, std::string>> listPeripherals() const;

	// Direct access (bypass peripheral dispatch, for snapshot iteration)
	uint32_t getSize() const { return size; }
	data_t readDirect(addr_t addr) const { return (addr < size) ? dmem[addr] : 0; }
	void writeDirect(addr_t addr, data_t val) { if (addr < size) dmem[addr] = val; }
};

///Instruction memory for ANEM
class ANEMInstructionMemory
{
private:
	ANEMInstruction * imem;
	uint32_t size;

public:
	ANEMInstructionMemory() {imem = nullptr, size=0; };
	ANEMInstructionMemory(uint32_t size);
	ANEMInstruction fetch(addr_t address);

	//load instructions from file; returns entry point word address (0 if not found)
	//dmem is optional; if provided, data sections (.rodata/.data) are loaded into it
	addr_t loadProgram(std::string fileName, ANEMDataMemory *dmem = nullptr);

private:
	bool loadELF(const std::string &fileName, addr_t &entryAddr, ANEMDataMemory *dmem);

};



#endif /* MEM_H_ */
