#include "waterServer.h"
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include "log4cxx/propertyconfigurator.h"
#include "log4cxx/helpers/exception.h"
#include <unistd.h> // sleep, lockf
#include <limits>
#include <boost/thread/scoped_thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

namespace waterServer
{

log4cxx::LoggerPtr logger{log4cxx::Logger::getLogger("waterServer")};

static int pidFd = -1;
#define PID_FILE_NAME "/var/run/waterServer.pid"

int applicationMain(
	std::string const & guiUrl,
	std::list<WaterClient::SlaveId> const & slaveIds,
	std::string const & device,
	int baud, char parity, int dataBits, int stopBits,
	int timeoutSec
)
{
	GuiProxy::GlobalInit();

	while (1)
	{
		LOG("starting application");
		try
		{
			std::unique_ptr<GuiProxy> const guiProxy = GuiProxy::CreateDefault(guiUrl);
			std::unique_ptr<ModbusServer> const modbusServer = ModbusServer::CreateDefault(
				device.c_str(), baud, parity, dataBits, stopBits, timeoutSec
			);
			std::unique_ptr<ClientProxy> const clientProxy = ClientProxy::CreateDefault(
				*guiProxy, *modbusServer, slaveIds);
			clientProxy->run();
		}
		catch (RestartNeededException const &)
		{
			LOG("server failed");
			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}
	}
	GuiProxy::GlobalCleanup();
	return 0;
}

std::list<WaterClient::SlaveId> makeSlavesArray(std::string const & s)
{
  std::list<WaterClient::SlaveId> result;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ','))
  {
	  auto slaveId = boost::lexical_cast<uint32_t>(item);
	  if (slaveId > std::numeric_limits<WaterClient::SlaveId>::max())
	  {
		  throw "too large id";
	  }
	  result.push_back(slaveId);
  }
  return result;
}

void signalHandler(int sig)
{
	if (sig == SIGINT)
	{
		LOG("stopping waterServer")

		if (pidFd != -1)
		{
			lockf(pidFd, F_ULOCK, 0);
			close(pidFd);
		}

		unlink(PID_FILE_NAME);

		_exit(0);
	}

	/* Reset signal handling to default behavior */
	signal(SIGINT, SIG_DFL);
}

}

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::cerr << "wrong number of parameters, provide configuration .ini file and logging .ini file\n";
		return 1;
	}

	WS_ASSERT(daemon(0, 0) == 0, "failed to daemonize");

	signal(SIGINT, waterServer::signalHandler);

	/* Try to write PID of daemon to lockfile */
	waterServer::pidFd = open(PID_FILE_NAME, O_RDWR|O_CREAT, 0640);
	WS_ASSERT(waterServer::pidFd > 0, "can not create/open pid file: " << PID_FILE_NAME);
	WS_ASSERT(lockf(waterServer::pidFd, F_TLOCK, 0) == 0, "can not lock pid file");

	char str[256];
	sprintf(str, "%d\n", getpid());
	write(waterServer::pidFd, str, strlen(str));



	boost::property_tree::ptree pt;
	boost::property_tree::ini_parser::read_ini(argv[1], pt);

	try
	{
		log4cxx::PropertyConfigurator::configure(argv[2]);
		syslog(LOG_INFO, "started waterServer");
		return waterServer::applicationMain(
			pt.get<std::string>("guiurl"),
			waterServer::makeSlavesArray(pt.get<std::string>("slaves")),
			pt.get<std::string>("device"),
			pt.get<int>("baud"),
			pt.get<char>("parity"),
			pt.get<int>("dataBits"),
			pt.get<int>("stopBits"),
			pt.get<int>("timeoutSec")
		);
	}
	catch(log4cxx::helpers::Exception const &)
	{
		std::cerr << "failed to initialize logging system\n";
		return 1;
	}
}
