#include "waterServer.h"

#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include <boost/thread/scoped_thread.hpp>
#include <boost/optional.hpp>
#include <boost/foreach.hpp>
//#include <boost/static_assert.hpp>

namespace waterServer
{

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

	std::unique_ptr<WaterClient::Request> readRequest(ModbusServer &);

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

	ClientProxyImpl(GuiProxy &, ModbusServer &, std::list<WaterClient::SlaveId> const &);

private:

	GuiProxy & guiProxy;
	ModbusServer & modbusServer;
	std::list<Slave> slaves;

	void processSlave(Slave &);
};

char Slave::buffer[SEND_BUFFER_SIZE_BYTES];

std::unique_ptr<WaterClient::Request>
Slave::readRequest(ModbusServer & ms)
{
	ms.setSlave(this->id);

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
		ms.writeRegisters(
			REPLY_ADDRESS, SEND_BUFFER_SIZE_BYTES/2, reinterpret_cast<uint16_t*>(Slave::buffer));
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

	auto rc = ms.readRegisters(REQUEST_ADDRESS, SEND_BUFFER_SIZE_BYTES/2, reinterpret_cast<uint16_t*>(Slave::buffer));
	if (rc == -1)
	{
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

ClientProxyImpl::ClientProxyImpl(
	GuiProxy & guiProxyArg, ModbusServer & modbusServerArg, std::list<WaterClient::SlaveId> const & slaveIdsArg) :
	guiProxy(guiProxyArg),
	modbusServer(modbusServerArg)
{
	BOOST_FOREACH(WaterClient::SlaveId const slaveId, slaveIdsArg)
	{
		this->slaves.push_back(slaveId);
	}

	//BOOST_STATIC_ASSERT((sizeof(water::WaterClient::Request) + sizeof(uint16_t) - 1) / sizeof(uint16_t) == SEND_BUFFER_SIZE_BYTES/2);
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
	auto requestPtr = slave.readRequest(this->modbusServer);

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
ClientProxy::CreateDefault(GuiProxy & guiProxy, ModbusServer & modbusServer, std::list<WaterClient::SlaveId> const & slaveIds)
{
	DLOG("pooling " << slaveIds.size() << " slaves");
	return std::unique_ptr<ClientProxy>(new ClientProxyImpl(guiProxy, modbusServer, slaveIds));
}

ClientProxy::~ClientProxy() = default;

}
