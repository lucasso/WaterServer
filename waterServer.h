#ifndef _WATER_SERVER
#define _WATER_SERVER
#include <iostream>

#include "waterSharedTypes.h"

#include <list>
#include <memory> // unique_ptr
#include <log4cxx/logger.h>

#define VERSION "1.0"

namespace waterServer
{

using water::WaterClient;

class RestartNeededException
{
	std::string whatStr;
public:
	RestartNeededException(std::string const & whatArg = "") : whatStr(whatArg) {}

	std::string const & what() const  { return this->whatStr; }
};

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

std::ostream & operator<<(std::ostream &, WaterClient::Request const &);
std::ostream & operator<<(std::ostream &, water::Reply const &);

extern log4cxx::LoggerPtr logger;

#define DLOG(msg) LOG4CXX_DEBUG(waterServer::logger, msg)
#define LOG(msg) LOG4CXX_INFO(waterServer::logger, msg)
#define ELOG(msg) LOG4CXX_ERROR(waterServer::logger, msg)
#define WLOG(msg) LOG4CXX_WARN(waterServer::logger, msg)

#define WS_ASSERT(cnd, msg) \
	if (!(cnd)) { \
		std::ostringstream osek; osek << "waterServer failed: " << msg; \
		::syslog(LOG_ERR, osek.str().c_str()); \
		std::cerr << "ASSERTION " << osek.str() << "\n"; \
		::abort(); \
	}
	
}

#define THROW_RESTART_NEEDED_IF(cnd, msg) \
	if (cnd) { \
		std::ostringstream osek; osek << msg; \
		throw RestartNeededException(osek.str()); \
	}

#endif // _WATER_SERVER
