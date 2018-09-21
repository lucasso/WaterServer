#include <iostream>
#include <modbus/modbus.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include <boost/static_assert.hpp>

#define _WATER_SERVER
#include "waterSharedTypes.h"
#undef _WATER_SERVER

// modbus functions documentation
// http://libmodbus.org/docs/v3.0.6/

#define DEVICE "/dev/water"
#define BAUD 9600
#define PARITY 'N'
#define DATA_BITS 8
#define STOP_BITS 1

#define REQUEST_TIMEOUT_SEC 2

class FatalError {};

class Master
{
  modbus_t * ctx;

public:
  Master() :
	ctx(modbus_new_rtu(DEVICE, BAUD, PARITY, DATA_BITS, STOP_BITS))
  {
	if (this->ctx == NULL)
	  {
		std::cerr << "Unable to create the libmodbus context\n";
		throw FatalError();
	  }

	if (modbus_connect(this->ctx) == -1) 
	  {
		std::cerr << "Connection failed: " << modbus_strerror(errno) << "\n";
		throw FatalError();		
	  }


	struct timeval timeoutValue = { REQUEST_TIMEOUT_SEC, 0 };
	
	modbus_set_response_timeout(this->ctx, &timeoutValue);
  }

  ~Master()
  {
	if (this->ctx != NULL) 
	  {
		modbus_close(this->ctx);
		modbus_free(this->ctx);
	  }
  }

  void setSlave(int);
  int readData();
  void writeData(int);

};

void Master::setSlave(int const slaveId)
{
  if (modbus_set_slave(this->ctx, slaveId) == -1)
	{
	  std::cerr << "setting slaveId failed\n";
	  throw FatalError();
	}
}

int Master::readData()
{
	uint16_t tab_reg[SEND_BUFFER_SIZE_BYTES/2];
	int rc;

  while (1)
	{
	  rc = modbus_read_registers(this->ctx, REQUEST_ADDRESS, SEND_BUFFER_SIZE_BYTES/2, tab_reg);
	  if (rc == -1)
		{
		  std::cerr << "reading register failed " << modbus_strerror(errno) << "\n";
		  //sleep(1);
		}
	  else break;
	}

  water::WaterClient::Request* rq = reinterpret_cast<water::WaterClient::Request*>(tab_reg);
  std::cout << "requestSeqNumAtBegin:" << +rq->requestSeqNumAtBegin << ", requestType:" << static_cast<uint32_t>(rq->requestType);
  switch (rq->requestType)
  {
  case water::RequestType::LOGIN_BY_USER:
	  std::cout << ", userId:" << rq->impl.loginByUser.userId << ", pin:" << rq->impl.loginByUser.userId;
	  break;

  case water::RequestType::LOGIN_BY_RFID:
	  std::cout << ", rfid:" << rq->impl.loginByRfid.rfidId ;
	  break;
  default:
	  std::cout << "unknown type";
  }

  std::cout << ", consumeCredit:" << rq->consumeCredit << ", requestSeqNumAtEnd:" << +rq->requestSeqNumAtEnd  << "\n";

  //for (int i=0; i < rc; i++) {
	//std::cout << "reg[" << i << "] = " << tab_reg[i] << "\n";
  //}

  return tab_reg[0];
}

void Master::writeData(int value)
{

  while (1)
	{
	  if (modbus_write_register(this->ctx, 0x7000, value) != 1)
		{
		  std::cerr << "writing register failed " << modbus_strerror(errno) << "\n";
		  sleep(1);
		}
	  else 
		{
		  std::cout << "writing value:" << value << " succeeded\n";
		  return;
		}
	}
}


int main()
{
	BOOST_STATIC_ASSERT((sizeof(water::WaterClient::Request) + sizeof(uint16_t) - 1) / sizeof(uint16_t) == SEND_BUFFER_SIZE_BYTES/2);


  Master m;
  m.setSlave(101);
  while(1)
  {
	  m.readData();
  }
  //m.writeData(val+1);
  //m.readData();
  return 0;
}
