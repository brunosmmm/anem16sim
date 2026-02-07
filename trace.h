/*
 * @file trace.h
 * @brief HW-compatible trace output (MW/RF/SR/END format)
 */

#ifndef TRACE_H_
#define TRACE_H_

#include <string>
#include <fstream>
#include <cstdint>

class ANEMCPU;  // forward decl

class TraceWriter {
public:
	TraceWriter();
	~TraceWriter();
	bool open(const std::string& path);
	void close();
	bool isOpen() const;
	void memoryWrite(uint16_t addr, uint16_t data);
	void finish(const ANEMCPU& cpu);
private:
	std::ofstream file;
	unsigned int mw_count = 0;
};

#endif /* TRACE_H_ */
