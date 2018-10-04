#define _WATER_SERVER
#include "waterSharedTypes.h"
#undef _WATER_SERVER

namespace waterServer
{

class GuiProxy
{
public:

	class Callback
	{
		virtual void serverInternalError() = 0;
		virtual void notFound() = 0;
		virtual void success(Credit creditAvail) = 0;

		virtual ~Callback() = default;
	};


	virtual void handleIdPinRequest(UserId userId, Pin pin, Credit creditToConsume, Callback*) = 0;
	virtual void handleRfidRequest(RfidId rfidId, Credit creditToConsume, Callback*) = 0;

	virtual ~GuiProxy() = default;
};

class ClientProxy
{
public:

	virtual ~ClientProxy() = default;

	static std::unique_ptr<ClientProxy> CreateDefault(RequestHandler &);

	virtual void stop();
};

	
}
a
