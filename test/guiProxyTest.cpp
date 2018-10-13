#include "../waterServer.h"
#include "log4cxx/basicconfigurator.h"
#include "log4cxx/helpers/exception.h"
#include <boost/thread/condition.hpp>

namespace waterServer
{

log4cxx::LoggerPtr logger{log4cxx::Logger::getLogger("waterServer")};

class GuiProxyTestCallback : public GuiProxy::Callback
{
	bool replyHasCome;
	boost::mutex mtx;
	boost::condition cnd;

	void notifyThatWeHaveReply()
	{
		{
			boost::mutex::scoped_lock lck(this->mtx);
			this->replyHasCome = true;
		}
		this->cnd.notify_all();
	}

	virtual void serverInternalError()
	{
		LOG("got serverInternalError");
		this->notifyThatWeHaveReply();
	}

	virtual void notFound()
	{
		LOG("got notFound");
		this->notifyThatWeHaveReply();
	}

	virtual void success(WaterClient::Credit creditAvail)
	{
		LOG("got success");
		this->notifyThatWeHaveReply();
	}

public:

	GuiProxyTestCallback() : replyHasCome(false) {}

	void waitForReply()
	{
		boost::mutex::scoped_lock lck(this->mtx);
		while(!this->replyHasCome) this->cnd.wait(lck);
		this->replyHasCome = false;
	}

};
void guiProxyTest(GuiProxy & gp)
{
	GuiProxyTestCallback cb;
	gp.handleIdPinRequest(1579, 9191, 1001, &cb);
	cb.waitForReply();
}


int guiProxyTestMain()
{
	try
	{
		// Set up a simple configuration that logs on the console.
		log4cxx::BasicConfigurator::configure();

		LOG("Staring GuiProxy test");
		guiProxyTest(*GuiProxy::CreateDefault("http://localhost:3000"));

	}
	catch(log4cxx::helpers::Exception&)
	{
		return 1;
	}

	return 0;
}


}


int main()
{
	return waterServer::guiProxyTestMain();
}
