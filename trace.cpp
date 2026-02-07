/*
 * @file trace.cpp
 * @brief HW-compatible trace output (MW/RF/SR/END format)
 *
 * Format matches anem16pipe/tests/TRACE_FORMAT.md:
 *   # anem16-trace v1
 *   MW <addr> <data>     -- on every SW commit (4-digit hex, lowercase, zero-padded)
 *   RF <idx> <value>     -- register dump at end (idx decimal 0-15, value 4-digit hex)
 *   SR <name> <value>    -- HI/LO at end
 *   END <mw_count>       -- final line, decimal MW count
 */

#include "trace.h"
#include "cpu.h"
#include <iomanip>

TraceWriter::TraceWriter() = default;

TraceWriter::~TraceWriter()
{
	close();
}

bool TraceWriter::open(const std::string& path)
{
	file.open(path, std::ios::out | std::ios::trunc);
	if (!file.is_open())
		return false;

	file << "# anem16-trace v1" << std::endl;
	mw_count = 0;
	return true;
}

void TraceWriter::close()
{
	if (file.is_open())
		file.close();
}

bool TraceWriter::isOpen() const
{
	return file.is_open();
}

void TraceWriter::memoryWrite(uint16_t addr, uint16_t data)
{
	// Exclude MAC peripheral writes (addr >= 0xFFD0)
	if (addr >= 0xFFD0)
		return;

	file << "MW " << std::hex << std::setfill('0') << std::setw(4) << addr
	     << " " << std::setw(4) << data << std::endl;
	mw_count++;
}

void TraceWriter::finish(const ANEMCPU& cpu)
{
	// Register file dump
	for (int i = 0; i < 16; i++)
	{
		file << "RF " << std::dec << i << " "
		     << std::hex << std::setfill('0') << std::setw(4)
		     << cpu.readRegister(i) << std::endl;
	}

	// Special registers
	file << "SR HI " << std::hex << std::setfill('0') << std::setw(4)
	     << cpu.getHI() << std::endl;
	file << "SR LO " << std::hex << std::setfill('0') << std::setw(4)
	     << cpu.getLO() << std::endl;
	file << "SR SP " << std::hex << std::setfill('0') << std::setw(4)
	     << cpu.getSP() << std::endl;

	// End marker with MW count
	file << "END " << std::dec << mw_count << std::endl;
}
