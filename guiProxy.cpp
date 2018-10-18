#include "waterServer.h"

#include <boost/thread/scoped_thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>
#include <curl/curl.h>

namespace waterServer
{

namespace pt = boost::property_tree;

struct GuiRequest
{
	std::string path;
	std::string postParams;
	pt::ptree responseRoot;

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
		this->requests.push_back(GuiRequest{this->urlPrefix + "/" + urlRequestName, urlRequestParams, {}, callback});
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


static size_t dataReceived(const void *ptr, size_t size, size_t nmemb, void *userp)
{
	try
	{
		size_t sizeOfAvailData = size*nmemb;

		std::stringstream ss;
		ss << std::string{static_cast<char const *>(ptr), sizeOfAvailData};
		boost::property_tree::read_json(ss, *static_cast<pt::ptree*>(userp));
		return sizeOfAvailData;
	}
	catch (pt::json_parser::json_parser_error const & parseError)
	{
		ELOG("could not parse response, " << parseError.what());
		return 0;
	}
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
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, dataReceived);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &requestToProcess.responseRoot);
		//curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 1L);

		CURLcode res = curl_easy_perform(curl.get());
	
		switch (res)
		{
		case CURLE_OK:
		{
			long httpCode = 0;
			curl_easy_getinfo (curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);

			if (httpCode == 200) // OK
			{
				try
				{
					int32_t const creditsAvail = requestToProcess.responseRoot.get<int32_t>("credit");
					LOG("success, creditsAvail:" << creditsAvail);
					requestToProcess.callback->success(creditsAvail);
				}
				catch (pt::ptree_bad_path const &)
				{
					ELOG("there is no \"credit\" field in success response, failing request");
					requestToProcess.callback->serverInternalError();
				}

			}
			else if (httpCode == 404) // Not found
			{
				LOG("not found");
				requestToProcess.callback->notFound();
			}
			else
			{
				ELOG("internal error, httpCode:" << httpCode);
				requestToProcess.callback->serverInternalError();
			}
			break;
		}
		default:
			LOG("request failed, curlCode:" << res << ", error:" << curl_easy_strerror(res));
			requestToProcess.callback->serverInternalError();
			break;
		}
	}
}


}
