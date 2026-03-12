/*
 * @file web_server.h
 * @brief ANEM16 web debug server — JSON-RPC 2.0 over WebSocket + HTML UI
 *        Uses libwebsockets for HTTP and WebSocket transport.
 */

#ifndef WEB_SERVER_H_
#define WEB_SERVER_H_

#include "debug_engine.h"
#include "jsonrpc.h"
#include <libwebsockets.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>

class ANEMWebServer
{
private:
	DebugEngine& engine;
	int port;
	std::string webRoot;
	std::string htmlContent;  // loaded from web/index.html
	std::string htmlFilePath; // resolved path for live reload
	std::filesystem::file_time_type htmlLastMod{}; // last modification time
	struct lws_context* lwsCtx = nullptr;

	// Source file cache for C source display
	std::string sourceRoot;
	std::unordered_map<std::string, std::vector<std::string>> sourceCache;
	const std::string& getSourceLine(const std::string& file, int line);

	// Static file content
	std::string isaRefContent; // loaded from web/isa_ref.json

	// HTTP session state per connection
	struct HttpSession { size_t pos; const std::string* content; };
	std::unordered_map<struct lws*, HttpSession> httpSessions;

	// WebSocket client tracking
	std::vector<struct lws*> wsClients;
	std::unordered_map<struct lws*, std::queue<std::string>> wsSendQueues;
	std::unordered_map<struct lws*, std::string> wsRecvBufs;

	// Async simulation
	std::jthread simThread;
	std::atomic<bool> simRunning{false};
	std::atomic<bool> pauseRequested{false};
	std::atomic<bool> externalStateChanged{false};
	std::mutex notifyMutex;
	std::queue<std::string> notifyQueue;

	// Single lws callback for both HTTP and WS
	static int lwsCallback(struct lws* wsi, enum lws_callback_reasons reason,
	                       void* user, void* in, size_t len);

	// JSON-RPC dispatch
	jsonrpc::json dispatch(const std::string& method,
	                        const jsonrpc::json& params,
	                        const jsonrpc::json& id);

	// RPC handlers
	jsonrpc::json handleStep(const jsonrpc::json& params);
	jsonrpc::json handleContinue(const jsonrpc::json& id);
	jsonrpc::json handlePause();
	jsonrpc::json handleRegisters(const jsonrpc::json& params);
	jsonrpc::json handleMemoryRead(const jsonrpc::json& params);
	jsonrpc::json handleMemoryWrite(const jsonrpc::json& params);
	jsonrpc::json handleDisassemble(const jsonrpc::json& params);
	jsonrpc::json handlePipeline();
	jsonrpc::json handleStats();
	jsonrpc::json handleStatus();
	jsonrpc::json handleReset();
	jsonrpc::json handleBreakpointAdd(const jsonrpc::json& params);
	jsonrpc::json handleBreakpointRemove(const jsonrpc::json& params);
	jsonrpc::json handleBreakpointList();
	jsonrpc::json handleWatchpointAdd(const jsonrpc::json& params);
	jsonrpc::json handleWatchpointRemove(const jsonrpc::json& params);
	jsonrpc::json handleWatchpointList();
	jsonrpc::json handleInterrupt(const jsonrpc::json& params);
	jsonrpc::json handlePeriphList();
	jsonrpc::json handleGPIORead(const jsonrpc::json& params);
	jsonrpc::json handleGPIOWrite(const jsonrpc::json& params);
	jsonrpc::json handleUartInject(const jsonrpc::json& params);
	jsonrpc::json handleTrace(const jsonrpc::json& params);
	jsonrpc::json handleSnapshotSave(const jsonrpc::json& params);
	jsonrpc::json handleSnapshotLoad(const jsonrpc::json& params);

	// Simulation worker
	void simLoop(std::stop_token stoken);
	void drainNotifications();
	void checkLiveReload();

	// Helpers
	static std::string toHex(uint32_t val, int width = 4);
	static uint32_t fromHex(const jsonrpc::json& val);
	static jsonrpc::json serializeStepResult(const StepResult& r);

public:
	ANEMWebServer(DebugEngine& engine, int port = 8080);
	~ANEMWebServer();
	void run();

	void setSourceRoot(const std::string& dir) { sourceRoot = dir; }
};

#endif /* WEB_SERVER_H_ */
