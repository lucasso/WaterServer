#include <iostream>
#include <modbus/modbus.h>
#include <errno.h>
#include <unistd.h>

// modbus functions documentation
// http://libmodbus.org/docs/v3.1.4/

#define DEVICE "/dev/ttyUSB0"
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

	if (modbus_set_response_timeout(this->ctx, REQUEST_TIMEOUT_SEC, 0) == -1)
	  {
		std::cerr << "Setting request timeout failed\n";
		throw FatalError();
	  }
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
  uint16_t tab_reg[1];
  int rc;

  while (1)
	{
	  rc = modbus_read_registers(this->ctx, 0x7000, 1, tab_reg);
	  if (rc == -1)
		{
		  std::cerr << "reading register failed " << modbus_strerror(errno) << "\n";
		  sleep(1);
		}
	  else break;
	}

  for (int i=0; i < rc; i++) {
	std::cout << "reg[" << i << "] = " << tab_reg[i] << "\n";
  }

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
  Master m;
  m.setSlave(101);
  int val = m.readData();
  m.writeData(val+1);
  m.readData();
  return 0;
}
