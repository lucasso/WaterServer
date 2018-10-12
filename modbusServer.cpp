#include "waterServer.h"
#include <modbus/modbus.h>
#include <boost/assert.hpp>

namespace waterServer
{

class ModbusServerImpl : public ModbusServer
{
	std::unique_ptr<modbus_t, void(*)(modbus_t*)> ctx;

public:

	ModbusServerImpl(const char *device, int baud, char parity, int data_bit, int stop_bit, int timeoutSec) :
		ctx(modbus_new_rtu(device, baud, parity, data_bit, stop_bit), modbus_free)
	{
		if (this->ctx.get() == nullptr)
		{
			ELOG("unable to create the libmodbus context, " << modbus_strerror(errno));
			throw RestartNeededException();
		}

		auto connectResult = modbus_connect(this->ctx.get());
		if (connectResult == -1)
		{
			ELOG("modbus_connect failed: " << modbus_strerror(errno));
			throw RestartNeededException();
		}

		struct timeval timeoutValue = { timeoutSec, 0 };
		modbus_set_response_timeout(this->ctx.get(), &timeoutValue);
	}

	virtual ~ModbusServerImpl()
	{
		LOG("closing modbus connection");
		modbus_close(this->ctx.get());
	}

	virtual void setSlave(int id)
	{
		auto setSlaveResult = modbus_set_slave(this->ctx.get(), id);
		BOOST_ASSERT_MSG(setSlaveResult != -1, "setting slaveId failed");
	}

	virtual int readRegisters(int addr, int nb, uint16_t *dest)
	{
		auto retVal = modbus_read_registers(this->ctx.get(), addr, nb, dest);
		if (retVal == -1)
		{
			DLOG("modbus reading failed " << modbus_strerror(errno));
		}
		return retVal;
	}

	virtual int writeRegisters(int addr, int nb, const uint16_t *data)
	{
		auto retVal = modbus_write_registers(this->ctx.get(), addr, nb, data);
		if (retVal != 1)
		{
			ELOG("modbus writing failed " << modbus_strerror(errno));
		}
		return retVal;
	}

};


ModbusServer::~ModbusServer() = default;

std::unique_ptr<ModbusServer> ModbusServer::CreateDefault(
	const char *device, int baud, char parity, int data_bit, int stop_bit, int timeoutSec)
{
	return std::unique_ptr<ModbusServer>(new ModbusServerImpl(
		device, baud, parity, data_bit, stop_bit, timeoutSec));
}

} // ns waterServer
