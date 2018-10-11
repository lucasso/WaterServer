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

	Slave(WaterClient::SlaveId idArg) :
		id(idArg), processingInGui(false), lastReceivedSeqNum(0)
	{}

	Slave(Slave && other) :
		replyToSendProtected(std::move(other.replyToSendProtected)),
		replyToSend(std::move(other.replyToSend)),
		id(other.id), processingInGui(other.processingInGui),
		lastReceivedSeqNum(other.lastReceivedSeqNum)
	{
	}

	~Slave() = default;

	std::unique_ptr<WaterClient::Request> readRequest(modbus_t* ctx);

	template <class T> static void readWriteRequest(T &, T);
	template <class T> static void readWriteReply(T, T &);

private:

	boost::mutex mtx;
	std::unique_ptr<water::Reply> replyToSendProtected;
	std::unique_ptr<water::Reply> replyToSend;

	WaterClient::SlaveId id;
	bool processingInGui;
	WaterClient::RequestSeqNum lastReceivedSeqNum;

	virtual void serverInternalError();
	virtual void notFound();
	virtual void success(WaterClient::Credit creditAvail);

	void setReply(
		WaterClient::LoginReply::Status,
		WaterClient::Credit creditAvail = 0);

	static char buffer[SEND_BUFFER_SIZE_BYTES];
};

class ClientProxyImpl : public ClientProxy
{
	virtual void run();

public:

	ClientProxyImpl(GuiProxy &, std::list<WaterClient::SlaveId> const &);
	~ClientProxyImpl();

private:

	GuiProxy & guiProxy;
	std::unique_ptr<modbus_t, void(*)(modbus_t*)> ctx;
	std::list<Slave> slaves;

	void processSlave(Slave &);
};

char Slave::buffer[SEND_BUFFER_SIZE_BYTES];

std::unique_ptr<WaterClient::Request>
Slave::readRequest(modbus_t* ctx)
{
	auto setSlaveResult = modbus_set_slave(ctx, this->id);
	BOOST_ASSERT_MSG(setSlaveResult != -1, "setting slaveId failed");

	if (this->processingInGui)
	{
		boost::mutex::scoped_lock lck(this->mtx);
		if (this->replyToSendProtected.get() != nullptr)
		{
			this->replyToSend = std::move(this->replyToSendProtected);
			this->processingInGui = false;
		}
	}

	if (this->replyToSend.get())
	{
		DLOG("sending reply to slave num " << +this->id);
		water::serializeReply<Slave>(*this->replyToSend, Slave::buffer);

		if (modbus_write_registers(
			ctx, REPLY_ADDRESS, SEND_BUFFER_SIZE_BYTES/2,
			reinterpret_cast<uint16_t*>(Slave::buffer)) != 1)
		{
			ELOG("writing reply failed " << modbus_strerror(errno));
		}
//		else
//		{
//			DLOG("success writing reply:" << *this->replyToSend);
//		}
	}

	if (this->processingInGui)
	{
		DLOG("skipped processing slave num " << +this->id << " because its request is being handled in GUI");
		return std::unique_ptr<WaterClient::Request>();
	}

	DLOG("trying to read request from slave:" << +this->id);

	auto rc = modbus_read_registers(ctx, REQUEST_ADDRESS, SEND_BUFFER_SIZE_BYTES/2, reinterpret_cast<uint16_t*>(Slave::buffer));
	if (rc == -1)
	{
		DLOG("reading from slave failed, " << modbus_strerror(errno));
		return std::unique_ptr<WaterClient::Request>();
	}

	std::unique_ptr<water::WaterClient::Request> rq{new water::WaterClient::Request()};
	bool const serializeSuccess = water::serializeRequest<Slave>(*rq, Slave::buffer);
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

	DLOG("request from slave num " << (+this->id));
	if (rq->requestSeqNumAtBegin == this->lastReceivedSeqNum)
	{
		DLOG("ignoring already received message with seqNum:" << this->lastReceivedSeqNum);
		return std::unique_ptr<WaterClient::Request>();
	}

	this->lastReceivedSeqNum = rq->requestSeqNumAtBegin;
	this->processingInGui = true;

	return std::move(rq);
}

void Slave::setReply(
	WaterClient::LoginReply::Status const status,
	WaterClient::Credit const creditAvail)
{
	std::unique_ptr<water::Reply> reply{ new water::Reply{
		this->lastReceivedSeqNum,
		WaterClient::LoginReply{status, creditAvail},
		this->lastReceivedSeqNum}};

	{
		boost::mutex::scoped_lock lck(this->mtx);
		this->replyToSendProtected.swap(reply);
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

std::ostream & operator<<(std::ostream & osek, water::Reply const & reply)
{
	osek << "{sqNum:" << +reply.replySeqNumAtBegin << ",status:";
	switch (reply.impl.status)
	{
	case WaterClient::LoginReply::Status::SUCCESS:
		osek << "SUCCESS"; break;
	case WaterClient::LoginReply::Status::NOT_FOUND:
		osek << "NOT_FOUND"; break;
	case WaterClient::LoginReply::Status::TIMEOUT:
		osek << "TIMEOUT"; break;
	case WaterClient::LoginReply::Status::SERVER_INTERNAL_ERROR:
		osek << "SERVER_INTERNAL_ERROR"; break;
	default:
		osek << "unknownStatus:" << static_cast<uint32_t>(reply.impl.status);
	}
	osek << ",creditAvail:" << reply.impl.creditAvail << "}";
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
	auto requestPtr = slave.readRequest(this->ctx.get());

	if (requestPtr.get() == nullptr)
	{
		return;
	}

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
Slave::readWriteRequest(T & inMem, T const inBuffer)
{
	inMem = inBuffer;
}

template <class T> void
Slave::readWriteReply(T const inMem, T & inBuffer)
{
	inBuffer = inMem;
}


std::unique_ptr<ClientProxy>
ClientProxy::CreateDefault(GuiProxy & guiProxy, std::list<WaterClient::SlaveId> const & slaveIds)
{
	DLOG("pooling " << slaveIds.size() << " slaves");
	return std::unique_ptr<ClientProxy>(new ClientProxyImpl(guiProxy, slaveIds));
}

ClientProxy::~ClientProxy() = default;

}
