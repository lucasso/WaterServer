#include <iostream>

#define _WATER_SERVER
#include "waterSharedTypes.h"
#undef _WATER_SERVER

#include <list>
#include <memory> // unique_ptr
#include <log4cxx/logger.h>

namespace waterServer
{

using water::WaterClient;

class RestartNeededException {};

class GuiProxy
{
public:

	class Callback
	{
		virtual void serverInternalError() = 0;
		virtual void notFound() = 0;
		virtual void success(WaterClient::Credit creditAvail) = 0;

		virtual ~Callback() = default;
	};


	virtual void handleIdPinRequest(WaterClient::UserId, WaterClient::Pin, WaterClient::Credit creditToConsume, Callback*) = 0;
	virtual void handleRfidRequest(WaterClient::RfidId, WaterClient::Credit creditToConsume, Callback*) = 0;

	static std::unique_ptr<GuiProxy> CreateDefault();
	static void GlobalInit();
	static void GlobalCleanup();

	virtual ~GuiProxy() = default;
};

class ClientProxy
{
public:

	virtual ~ClientProxy();

	static std::unique_ptr<ClientProxy> CreateDefault(GuiProxy &, std::list<WaterClient::SlaveId> const &);

	virtual void run() = 0;
};

extern log4cxx::LoggerPtr logger;

#define DLOG(msg) LOG4CXX_DEBUG(logger, msg)
#define LOG(msg) LOG4CXX_INFO(logger, msg)
#define ELOG(msg) LOG4CXX_ERROR(logger, msg)
#define WLOG(msg) LOG4CXX_WARN(logger, msg)

#define WS_ASSERT(cnd, msg) \
	if (cnd) { std::cerr << "ASSERTION FAILED : " << msg << "\n"; ::abort(); }
	
}

