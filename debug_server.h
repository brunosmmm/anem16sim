/*
 * @file debug_server.h
 * @brief ANEM16 remote debug server — JSON-RPC 2.0 over ZeroMQ
 */

#ifndef DEBUG_SERVER_H_
#define DEBUG_SERVER_H_

#include "debug_engine.h"
#include "jsonrpc.h"
#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>

class ANEMDebugServer
{
private:
	DebugEngine engine;
	zmq::context_t zmqCtx;
	zmq::socket_t repSocket;
	zmq::socket_t pubSocket;
	int port;

	// Async continue state
	std::jthread simThread;
	std::atomic<bool> simRunning{false};
	std::atomic<bool> pauseRequested{false};

	// Notification queue (worker -> main)
	std::mutex notifyMutex;
	std::queue<jsonrpc::json> notifyQueue;

	// Main loop internals
	void drainNotifications();
	jsonrpc::json dispatch(const std::string& method, const jsonrpc::json& params,
	                        const jsonrpc::json& id);

	// RPC handlers
	jsonrpc::json handleStep(const jsonrpc::json& params);
	jsonrpc::json handleContinue(const jsonrpc::json& id);
	jsonrpc::json handlePause();
	jsonrpc::json handleBreakpointAdd(const jsonrpc::json& params);
	jsonrpc::json handleBreakpointRemove(const jsonrpc::json& params);
	jsonrpc::json handleBreakpointList();
	jsonrpc::json handleWatchpointAdd(const jsonrpc::json& params);
	jsonrpc::json handleWatchpointRemove(const jsonrpc::json& params);
	jsonrpc::json handleWatchpointList();
	jsonrpc::json handleRegisters(const jsonrpc::json& params);
	jsonrpc::json handleMemoryRead(const jsonrpc::json& params);
	jsonrpc::json handleMemoryWrite(const jsonrpc::json& params);
	jsonrpc::json handleDisassemble(const jsonrpc::json& params);
	jsonrpc::json handlePipeline();
	jsonrpc::json handleTrace(const jsonrpc::json& params);
	jsonrpc::json handleStats();
	jsonrpc::json handleReset();
	jsonrpc::json handleStatus();

	// Simulation worker
	void simLoop(std::stop_token stoken);

	// Serialization helpers
	static std::string toHex(uint32_t val, int width = 4);
	static uint32_t fromHex(const jsonrpc::json& val);
	static jsonrpc::json serializeStepResult(const StepResult& r);

public:
	ANEMDebugServer(ANEMCPU& cpu, int port = 6808);
	void run();
};

#endif /* DEBUG_SERVER_H_ */
