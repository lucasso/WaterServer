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

class GuiProxyImpl : public GuiProxy
{

	virtual void handleIdPinRequest(WaterClient::UserId userId, WaterClient::Pin pin, WaterClient::Credit creditToConsume, GuiProxy::Callback*);
	virtual void handleRfidRequest(WaterClient::RfidId rfidId, WaterClient::Credit creditToConsume, GuiProxy::Callback*);

public:

	GuiProxyImpl();
	~GuiProxyImpl();

private:

	void workerMain();

	CURL *curl;
	std::string const urlPrefix;
	boost::scoped_thread<> worker;
	
	std::list<GuiRequest> requests;
	boost::mutex mtx;
	boost::condition cnd;

};


std::unique_ptr<GuiProxy>
GuiProxy::CreateDefault()
{
	return std::unique_ptr<GuiProxy>(new GuiProxyImpl());
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

void GuiProxyImpl::handleIdPinRequest(WaterClient::UserId userId, WaterClient::Pin pin, WaterClient::Credit creditToConsume, GuiProxy::Callback* callback)
{
	std::string params = "client_id=" + boost::lexical_cast<std::string>(userId) + "&pin=" + boost::lexical_cast<std::string>(pin);
	if (creditToConsume > 0) params += "&=consumed_credit=" + boost::lexical_cast<std::string>(creditToConsume);
	this->requests.push_back(GuiRequest{this->urlPrefix + "getuser_idpin", params, callback});
}

void GuiProxyImpl::handleRfidRequest(WaterClient::RfidId rfidId, WaterClient::Credit creditToConsume, GuiProxy::Callback* callback)
{
	std::string params = "client_rfid=" + boost::lexical_cast<std::string>(rfidId);
	if (creditToConsume > 0) params += "&=consumed_credit=" + boost::lexical_cast<std::string>(creditToConsume);
	this->requests.push_back(GuiRequest{this->urlPrefix + "getuser_rfid", params, callback});	
}

GuiProxyImpl::GuiProxyImpl() :
	curl(curl_easy_init()),
	urlPrefix("http://localhost:3000/"),
	worker{boost::thread(&GuiProxyImpl::workerMain, this)}
{
	BOOST_ASSERT_MSG(curl, "curl initialization failed");
}


GuiProxyImpl::~GuiProxyImpl()
{
	this->worker.interrupt();
	this->worker.join();
	curl_easy_cleanup(this->curl);
}


void
GuiProxyImpl::workerMain()
{
	while (1) {
		
		curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:3000/getuser_rfid");	
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "client_rfid=96337");
 

		CURLcode res = curl_easy_perform(curl);
	
		if (res != CURLE_OK) {
			LOG("curl_easy_perform() failed: " << curl_easy_strerror(res));
		}
		else {
			LOG("curl success");
		}
		boost::this_thread::sleep(boost::posix_time::seconds(1));
	}
}


}
