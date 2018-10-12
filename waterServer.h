#ifndef _WATER_SERVER
#define _WATER_SERVER
#include <iostream>


#include "waterSharedTypes.h"

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
	public:

		virtual void serverInternalError() = 0;
		virtual void notFound() = 0;
		virtual void success(WaterClient::Credit creditAvail) = 0;

		virtual ~Callback();
	};


	virtual void handleIdPinRequest(WaterClient::UserId, WaterClient::Pin, WaterClient::Credit creditToConsume, Callback*) = 0;
	virtual void handleRfidRequest(WaterClient::RfidId, WaterClient::Credit creditToConsume, Callback*) = 0;

	static std::unique_ptr<GuiProxy> CreateDefault(std::string const & guiUrl);
	static void GlobalInit();
	static void GlobalCleanup();

	virtual ~GuiProxy() = default;
};

class ModbusServer
{
public:
	virtual ~ModbusServer();

	virtual void setSlave(int) = 0;
	virtual int readRegisters(int addr, int nb, uint16_t *dest) = 0;
	virtual int writeRegisters(int addr, int nb, const uint16_t *data) = 0;

	static std::unique_ptr<ModbusServer> CreateDefault(
		const char *device, int baud, char parity, int data_bit, int stop_bit, int timeoutSec);
};

class ClientProxy
{
public:

	virtual ~ClientProxy();

	static std::unique_ptr<ClientProxy> CreateDefault(GuiProxy &, ModbusServer &, std::list<WaterClient::SlaveId> const &);

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

#endif // _WATER_SERVER
