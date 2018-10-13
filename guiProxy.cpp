#include "waterServer.h"

#include <boost/thread/scoped_thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/lexical_cast.hpp>
#include <curl/curl.h>

namespace waterServer
{

struct GuiRequest
{
	std::string path;
	std::string postParams;
	GuiProxy::Callback* callback;
};

std::ostream & operator<<(std::ostream & osek, GuiRequest const & rq)
{
	osek << "{url:" << rq.path << ",params:" << rq.postParams << "}";
	return osek;
}

class GuiProxyImpl : public GuiProxy
{

	virtual void handleIdPinRequest(WaterClient::UserId userId, WaterClient::Pin pin, WaterClient::Credit creditToConsume, GuiProxy::Callback*);
	virtual void handleRfidRequest(WaterClient::RfidId rfidId, WaterClient::Credit creditToConsume, GuiProxy::Callback*);

	void handleRequestImpl(
		std::string urlRequestName, std::string urlRequestParams,
		WaterClient::Credit creditToConsume, GuiProxy::Callback* callback);

public:

	GuiProxyImpl(std::string const & guiUrl);
	~GuiProxyImpl();

private:

	void workerMain();

	std::string const urlPrefix;
	boost::scoped_thread<> worker;
	
	std::list<GuiRequest> requests;
	boost::mutex mtx;
	boost::condition cnd;

};

GuiProxy::Callback::~Callback() = default;

std::unique_ptr<GuiProxy>
GuiProxy::CreateDefault(std::string const & guiUrl)
{
	return std::unique_ptr<GuiProxy>(new GuiProxyImpl(guiUrl));
}

void
GuiProxy::GlobalInit()
{
	curl_global_init(CURL_GLOBAL_NOTHING);
}

void
GuiProxy::GlobalCleanup()
{
	curl_global_cleanup();
}

void
GuiProxyImpl::handleIdPinRequest(WaterClient::UserId userId, WaterClient::Pin pin, WaterClient::Credit creditToConsume, GuiProxy::Callback* callback)
{
	this->handleRequestImpl(
		"getuser_idpin",
		std::string("client_id=") + boost::lexical_cast<std::string>(userId) + "&pin=" + boost::lexical_cast<std::string>(pin),
		creditToConsume,
		callback);
}

void GuiProxyImpl::handleRfidRequest(WaterClient::RfidId rfidId, WaterClient::Credit creditToConsume, GuiProxy::Callback* callback)
{
	this->handleRequestImpl(
		"getuser_rfid",
		std::string("client_rfid=") + boost::lexical_cast<std::string>(rfidId),
		creditToConsume,
		callback);
}

void
GuiProxyImpl::handleRequestImpl(
	std::string urlRequestName, std::string urlRequestParams, WaterClient::Credit creditToConsume, GuiProxy::Callback* callback)
{
	if (creditToConsume > 0) urlRequestParams += "&=consumed_credit=" + boost::lexical_cast<std::string>(creditToConsume);

	{
		boost::mutex::scoped_lock lck(this->mtx);
		this->requests.push_back(GuiRequest{this->urlPrefix + "/" + urlRequestName, urlRequestParams, callback});
	}
	this->cnd.notify_one();
}



GuiProxyImpl::GuiProxyImpl(std::string const & urlPrefixArg) :
	urlPrefix(urlPrefixArg),
	worker{boost::thread(&GuiProxyImpl::workerMain, this)}
{
	LOG("using url: " << this->urlPrefix);
}


GuiProxyImpl::~GuiProxyImpl()
{
	this->worker.interrupt();
	this->worker.join();
}


void
GuiProxyImpl::workerMain()
{
	while (true)
	{
		GuiRequest requestToProcess;
		{
			boost::mutex::scoped_lock lck(this->mtx);
			while (this->requests.empty()) this->cnd.wait(lck);
			requestToProcess = this->requests.front();
			this->requests.pop_front();
		}

		LOG("sending request: " << requestToProcess);

		std::unique_ptr<CURL, void(*)(CURL*)> curl(curl_easy_init(), curl_easy_cleanup);
		BOOST_ASSERT_MSG(curl.get() != nullptr, "curl initialization failed");
		
		curl_easy_setopt(curl.get(), CURLOPT_URL, requestToProcess.path.c_str());
		curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, requestToProcess.postParams.c_str());
 
		CURLcode res = curl_easy_perform(curl.get());
	
		switch (res)
		{
		case CURLE_OK:
			LOG("success");
			requestToProcess.callback->success(13);
			break;

		default:
			LOG("request failed, curlCode:" << res << ", error:" << curl_easy_strerror(res));
			requestToProcess.callback->serverInternalError();
			break;
		}
	}
}


}
