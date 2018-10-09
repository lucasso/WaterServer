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

class Slave : public GuiProxy::Callback
{
public:
	WaterClient::SlaveId id;
	bool processingInGui;

	Slave(WaterClient::SlaveId idArg) :
		id(idArg), processingInGui(false), lastReceivedSeqNum(0)
	{}

	~Slave() = default;

private:

	boost::mutex mtx;
	std::unique_ptr<water::Reply> replyToSend;
	WaterClient::RequestSeqNum lastReceivedSeqNum;

	virtual void serverInternalError();
	virtual void notFound();
	virtual void success(WaterClient::Credit creditAvail);

	void setReply(
		WaterClient::LoginReply::Status,
		Option<WaterClient::Credit> creditAvail = Option<WaterClient::Credit>());
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

void Slave::setReply(
	WaterClient::LoginReply::Status status,
	Option<WaterClient::Credit> creditAvail)
{
	std::unique_ptr<water::Reply> reply{ new water::Reply{
		this->lastReceivedSeqNum,
		WaterClient::LoginReply{status, creditAvail},
		this->lastReceivedSeqNum}};

	{
		boost::mutex::scoped_lock lck(this->mtx);
		this->replyToSend.swap(reply);
	}

	BOOST_ASSERT_MSG(reply.get() == nullptr, "reply came while previus was not yet delivered");
}

void Slave::serverInternalError()
{
	this->setReply(WaterClient::LoginReply::Status::SERVER_INTERNAL_ERROR);
}

void Slave::notFound()
{
	this->setReply(WaterClient::LoginReply::Status::NOT_FOUND);
}

void Slave::success(WaterClient::Credit const creditAvail)
{
	this->setReply(WaterClient::LoginReply::Status::SUCCESS, creditAvail);
}

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
	auto setSlaveResult = modbus_set_slave(this->ctx.get(), slave.id);
	BOOST_ASSERT_MSG(setSlaveResult != -1, "setting slaveId failed");

	if (slave.replyToSend.get())
	{
		DLOG("sending reply to slave num " << +slave.id);
		slave.processingInGui = false;
		// TODO: implement

		return;
	}

	if (slave.processingInGui)
	{
		DLOG("skipped processing slave num " << +slave.id << " because its request is being handled in GUI");
		return;
	}

	DLOG("trying to reqd request from slave:" << +slave.id);
	auto requestPtr = this->readFromSlave();

	if (requestPtr.get() == nullptr)
	{
		DLOG("reading from slave num " << +slave.id << " failed");
		return;
	}

	DLOG("request from slave num " << +slave.id << " is " << *requestPtr);
	if (requestPtr->requestSeqNumAtBegin == slave.lastReceivedSeqNum)
	{
		DLOG("ignoring already received message with seqNum:" << slave.lastReceivedSeqNum);
		return;
	}

	slave.lastReceivedSeqNum = requestPtr->requestSeqNumAtBegin;
	slave.processingInGui = true;

	switch (requestPtr->requestType)
	{
	case water::RequestType::LOGIN_BY_USER:
		this->guiProxy.handleIdPinRequest(
			requestPtr->impl.loginByUser.userId,
			requestPtr->impl.loginByUser.pin,
			requestPtr->consumeCredit, &slave);
		return;
	case water::RequestType::LOGIN_BY_RFID:
		this->guiProxy.handleRfidRequest(
			requestPtr->impl.loginByRfid.rfidId,
			requestPtr->consumeCredit, &slave);
		return;
	}
	BOOST_ASSERT_MSG(false, "wrong request type");
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
	if (!serializeSuccess)
	{
		ELOG("failed to serialize request");
		return std::unique_ptr<WaterClient::Request>();
	}
	
	if (rq->requestSeqNumAtBegin != rq->requestSeqNumAtEnd)
	{
		ELOG("request ignored because seq nums do not match, start:"
			<< rq->requestSeqNumAtBegin << ", end:" << rq->requestSeqNumAtEnd);
		return std::unique_ptr<WaterClient::Request>();
	}

	return std::move(rq);
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
