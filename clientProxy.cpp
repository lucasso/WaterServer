#include "waterServer.h"
#include <modbus/modbus.h>

#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include <boost/assert.hpp>
#include <boost/thread/scoped_thread.hpp>
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


class ClientProxyImpl : public ClientProxy
{
	virtual void run();

public:

	ClientProxyImpl(GuiProxy &);
	~ClientProxyImpl();

private:

	GuiProxy & guiProxy;
	std::unique_ptr<modbus_t, void(*)(modbus_t*)> ctx;

	// jakies vektory - co odebrano, co wysylac, odpowiedzi pod lockiem

	void setSlave(int slaveId);

	// null on error
	std::unique_ptr<WaterClient::Request> readFromSlave();

	void sendToSlave();
	void writeToSlave();
};


ClientProxyImpl::ClientProxyImpl(GuiProxy & guiProxyArg) :
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
		DLOG("scanning slaves...");
		boost::this_thread::sleep(boost::posix_time::seconds(5));
	}
}

void
ClientProxyImpl::setSlave(int const slaveId)
{
	auto setSlaveResult = modbus_set_slave(this->ctx.get(), slaveId);
	BOOST_ASSERT_MSG(setSlaveResult != -1, "setting slaveId failed");
}

std::unique_ptr<WaterClient::Request>
ClientProxyImpl::readFromSlave()
{
	uint16_t tab_reg[SEND_BUFFER_SIZE_BYTES/2];
	auto rc = modbus_read_registers(this->ctx.get(), REQUEST_ADDRESS, SEND_BUFFER_SIZE_BYTES/2, tab_reg);
	if (rc == -1) {
		DLOG("reading from slave failed, " << modbus_strerror(errno));
		return std::unique_ptr<WaterClient::Request>();
	}

	std::unique_ptr<water::WaterClient::Request> rq{new water::WaterClient::Request()};

	rq->requestSeqNumAtBegin = *reinterpret_cast<WaterClient::RequestSeqNum*>(tab_reg);

	return std::move(rq);
	/*
  reinterpret_cast<water::WaterClient::Request*>(tab_reg);
  std::cout << "requestSeqNumAtBegin:" << +rq->requestSeqNumAtBegin << ", requestType:" << static_cast<uint32_t>(rq->requestType);
  switch (rq->requestType)
  {
  case water::RequestType::LOGIN_BY_USER:
	  std::cout << ", userId:" << rq->impl.loginByUser.userId << ", pin:" << rq->impl.loginByUser.userId;
	  break;

  case water::RequestType::LOGIN_BY_RFID:
	  std::cout << ", rfid:" << rq->impl.loginByRfid.rfidId ;
	  break;
  default:
	  std::cout << "unknown type";
  }

  std::cout << ", consumeCredit:" << rq->consumeCredit << ", requestSeqNumAtEnd:" << +rq->requestSeqNumAtEnd  << "\n";

  //for (int i=0; i < rc; i++) {
	//std::cout << "reg[" << i << "] = " << tab_reg[i] << "\n";
  //}

  return tab_reg[0];
	*/
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
ClientProxy::CreateDefault(GuiProxy & guiProxy)
{
	return std::unique_ptr<ClientProxy>(new ClientProxyImpl(guiProxy));
}

ClientProxy::~ClientProxy() = default;

}
