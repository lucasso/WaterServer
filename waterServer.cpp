#include "waterServer.h"

#include "log4cxx/propertyconfigurator.h"
#include "log4cxx/helpers/exception.h"
#include <unistd.h> // sleep

namespace waterServer
{

log4cxx::LoggerPtr logger{log4cxx::Logger::getLogger("waterServer")};

int applicationMain()
{
	GuiProxy::GlobalInit();

	while (1)
	{
		LOG("starting application");
		try
			{
				std::unique_ptr<GuiProxy> const guiProxy = GuiProxy::CreateDefault();
				std::unique_ptr<ClientProxy> const clientProxy = ClientProxy::CreateDefault(*guiProxy);
			}
		catch (RestartNeededException const &)
			{
				LOG("server failed");
				::sleep(1);
			}
		
	}
	GuiProxy::GlobalCleanup();
	return 0;
}


}
 
int main(int argc, char** argv)
{
	if (argc < 2)
		{
			std::cerr << "wrong number of parameters, provide logginin .ini file\n";
			return 1;
		}

	try
		{
			log4cxx::PropertyConfigurator::configure(argv[1]);
			return waterServer::applicationMain();
		}
	catch(log4cxx::helpers::Exception const &)
		{
			std::cerr << "failed to initialize logging system\n";
			return 0;
		}

}
