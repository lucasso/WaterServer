#include "waterServer.h"
#include <modbus/modbus.h>

#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include <boost/assert.hpp>
#include <boost/thread/scoped_thread.hpp>
#include <boost/optional.hpp>
#include <boost/foreach.hpp>
//#include <boost/static_assert.hpp>

namespace waterServer
{

// modbus functions documentation
// http://libmodbus.org/docs/v3.0.6/

#define DEVICE "/dev/water"
#define BAUD 9600
#define PARITY 'N'
#define DATA_BITS 8
#define STOP_BITS 1

#define REQUEST_TIMEOUT_SEC 2

struct Slave
{
	WaterClient::SlaveId id;
	bool processingInGui;
	boost::optional<water::Reply> replyToSend;

	Slave(WaterClient::SlaveId idArg) :
		id(idArg), processingInGui(false)
	{}
};

class ClientProxyImpl : public ClientProxy
{
	virtual void run();

public:

	ClientProxyImpl(GuiProxy &, std::list<WaterClient::SlaveId> const &);
	~ClientProxyImpl();

	template <class T>
	static void readWrite(T &, T const);

private:

	GuiProxy & guiProxy;
	std::unique_ptr<modbus_t, void(*)(modbus_t*)> ctx;
	std::list<Slave> slaves;

	char readBuffer[SEND_BUFFER_SIZE_BYTES];

	void processSlave(Slave &);

	// null on error
	std::unique_ptr<WaterClient::Request> readFromSlave();

	void sendToSlave();
	void writeToSlave();
};

std::ostream & operator<<(std::ostream & osek, WaterClient::Request const & rq)
{
	osek << "{sqNum:" << +rq.requestSeqNumAtBegin << ",";
	switch (rq.requestType)
	{
	case water::RequestType::LOGIN_BY_USER:
		osek << "userId:" << rq.impl.loginByUser.userId << ",pin:" << rq.impl.loginByUser.pin;
		break;
	case water::RequestType::LOGIN_BY_RFID:
		osek << "rfid:" << rq.impl.loginByRfid.rfidId;
		break;
	default:
		osek << "unknownType:" << static_cast<uint32_t>(rq.requestType);
	}
	osek << ",consumeCredit:" << rq.consumeCredit << ",sqNum:" << +rq.requestSeqNumAtEnd << "}";
	return osek;
}

ClientProxyImpl::ClientProxyImpl(GuiProxy & guiProxyArg, std::list<WaterClient::SlaveId> const & slaveIdsArg) :
	guiProxy(guiProxyArg),
	ctx(modbus_new_rtu(DEVICE, BAUD, PARITY, DATA_BITS, STOP_BITS), modbus_free)
{
	if (this->ctx.get() == nullptr) {
		ELOG("unable to create the libmodbus context, " << modbus_strerror(errno));
		throw RestartNeededException();
	}

	auto connectResult = modbus_connect(this->ctx.get());
	if (connectResult == -1) {
		ELOG("modbus_connect failed: " << modbus_strerror(errno));
		throw RestartNeededException();
	}

	struct timeval timeoutValue = { REQUEST_TIMEOUT_SEC, 0 };
	modbus_set_response_timeout(this->ctx.get(), &timeoutValue);

	BOOST_FOREACH(WaterClient::SlaveId const slaveId, slaveIdsArg)
	{
		this->slaves.push_back(slaveId);
	}

	//BOOST_STATIC_ASSERT((sizeof(water::WaterClient::Request) + sizeof(uint16_t) - 1) / sizeof(uint16_t) == SEND_BUFFER_SIZE_BYTES/2);
}

ClientProxyImpl::~ClientProxyImpl()
{
	LOG("closing modbus connection");
	modbus_close(this->ctx.get());
}

void
ClientProxyImpl::run()
{
	while (1)
	{
		BOOST_FOREACH(Slave & slave, this->slaves)
		{
			this->processSlave(slave);
			boost::this_thread::sleep(boost::posix_time::seconds(5));
		}
	}
}

void
ClientProxyImpl::processSlave(Slave & slave)
{
	if (slave.processingInGui)
	{
		DLOG("skipped processing slave num " << +slave.id << " because its request is being handled in GUI");
		return;
	}

	auto setSlaveResult = modbus_set_slave(this->ctx.get(), slave.id);
	BOOST_ASSERT_MSG(setSlaveResult != -1, "setting slaveId failed");

	if (slave.replyToSend.is_initialized())
	{
		DLOG("sending reply to slave num " << +slave.id);


		return;
	}

	DLOG("trying to reqd request from slave:" << +slave.id);
	auto requestPtr = this->readFromSlave();
	if (requestPtr.get() == nullptr)
	{
		DLOG("no request from slave num " << +slave.id);
	}
	else
	{
		DLOG("request from slave num " << +slave.id << " is " << *requestPtr);
	}

}

template <class T> void
ClientProxyImpl::readWrite(T & rqItem, T const rqValue)
{
	rqItem = rqValue;
}

std::unique_ptr<WaterClient::Request>
ClientProxyImpl::readFromSlave()
{
	auto rc = modbus_read_registers(this->ctx.get(), REQUEST_ADDRESS, SEND_BUFFER_SIZE_BYTES/2, reinterpret_cast<uint16_t*>(this->readBuffer));
	if (rc == -1)
	{
		DLOG("reading from slave failed, " << modbus_strerror(errno));
		return std::unique_ptr<WaterClient::Request>();
	}

	std::unique_ptr<water::WaterClient::Request> rq{new water::WaterClient::Request()};
	bool const serializeSuccess = water::serializeRequest<ClientProxyImpl>(*rq, this->readBuffer);
	
	return serializeSuccess ? std::move(rq) : std::unique_ptr<WaterClient::Request>();;
}


void
ClientProxyImpl::writeToSlave()
{
	int value = 8;
	if (modbus_write_register(this->ctx.get(), 0x7000, value) != 1)
		{
			std::cerr << "writing register failed " << modbus_strerror(errno) << "\n";
		}
	else 
		{
			std::cout << "writing value:" << value << " succeeded\n";
			return;
		}
}

std::unique_ptr<ClientProxy>
ClientProxy::CreateDefault(GuiProxy & guiProxy, std::list<WaterClient::SlaveId> const & slaveIds)
{
	return std::unique_ptr<ClientProxy>(new ClientProxyImpl(guiProxy, slaveIds));
}

ClientProxy::~ClientProxy() = default;

}
