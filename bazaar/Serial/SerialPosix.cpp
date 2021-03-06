#include "Serial.h"

#ifdef PLATFORM_POSIX

#include <SysExec/SysExec.h>

#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

NAMESPACE_UPP

static bool __IsSymLink(const char *path) 
{
	struct stat stf;
	lstat(path, &stf);
	return S_ISLNK(stf.st_mode);
}	


dword Serial::stdBauds[] = {
	0, 50, 75, 110, 134, 150,
	200, 300, 600, 1200, 1800,
	2400, 4800, 9600, 19200,
	38400, 57600, 115200, 230400, 345600, 460800
};

int Serial::stdBaudsCount = sizeof(Serial::stdBauds) / sizeof(dword);
	
// constructor
Serial::Serial()
{
	isError = false;
	errCode = Ok;
	fd = -1;
}

Serial::Serial(String const &port, dword speed, byte parity, byte bits, byte stopBits)
{
	isError = false;
	errCode = Ok;
	fd = -1;
	Open(port, speed, parity, bits, stopBits);
}

// destructor
Serial::~Serial()
{
	Close();
}

// open the port
bool Serial::Open(String const &port, dword lSpeed, byte parity, byte bits, byte stopBits)
{
	static int stdBaudCodes[] = {
		B0, B50, B75, B110, B134, B150,
		B200, B300, B600, B1200, B1800,
		B2400, B4800, B9600, B19200,
		B38400, B57600, B115200,B230400
	};

	// open the device
	fd = open(port, O_RDWR | O_NOCTTY /*| O_SYNC*/ | O_NDELAY);
	if (fd < 0)
	{
		fd = -1;
		isError = true;
		errCode = DeviceError;
		return false;
	}

	// sets interface parameters
	struct termios tty;
	memset(&tty, 0, sizeof tty);
	if (tcgetattr(fd, &tty) != 0)
	{
		Close();
		isError = true;
		errCode = DeviceError;
		return false;
	}
	
	//// CFLAG /////

	int idx = GetStandardBaudRates().Find(lSpeed);
	dword speed;
	if(idx < 0)
	{
		// custom speed
		// @@ maybe add support later
		Close();
		isError = true;
		errCode = InvalidSpeed;
		return false;
	}
	else
	{
		// standard speed
		speed = stdBaudCodes[idx];
		cfsetospeed(&tty, speed);
		cfsetispeed(&tty, speed);
	}

	// parity
	tty.c_cflag &= ~(PARENB | PARODD);
	switch(parity)
	{
		case ParityNone:
			break;
		case ParityEven:
			tty.c_cflag |= PARENB;
			break;
		case ParityOdd:
			tty.c_cflag |= PARENB | PARODD;
			break;
		case ParityMark:
			tty.c_cflag |= PARENB | PARODD | CMSPAR;
			break;
		case ParitySpace:
			tty.c_cflag |= PARENB | CMSPAR;
			break;
		default:
			Close();
			isError = true;
			errCode = InvalidParity;
			return false;
	}
	
	// set data size
	switch(bits)
	{
		case 5:
			tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS5;
			break;
		case 6:
			tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS6;
			break;
		case 7:
			tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS7;
			break;
		case 8:
			tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
			break;
		default:
			Close();
			isError = true;
			errCode = InvalidSize;
			return false;
	}
	
	// stop bits
	if(stopBits == 1)
		tty.c_cflag &= ~CSTOPB;
	else if(stopBits == 2)
		tty.c_cflag |= CSTOPB;
	else
	{
		Close();
		isError = true;
		errCode = InvalidStopBits;
		return false;
	}

	// ignore modem controls, enable reading
	tty.c_cflag |= (CLOCAL | CREAD);
	
	// no rts/cts
	tty.c_cflag &= ~CRTSCTS;

	//// LFLAG /////

	// no signaling chars, no echo, no canonical mode
	tty.c_lflag = 0;	
//	tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);	

	//// IFLAG /////

	// disable IGNBRK for mismatched speed tests; otherwise receive break as \000 chars
	// disable break processing
	tty.c_iflag &= ~IGNBRK;
//	tty.c_iflag |= IGNBRK;

	// DO NOT ignore carriage returns
	tty.c_iflag &= ~IGNCR;
	
	// DO NOT translate cr/lf
	tty.c_iflag &= ~ICRNL;
	
	// DO NOT translate cr
	tty.c_iflag &= ~INLCR;
	
	// shut off xon/xoff ctrl
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	
	//// OFLAG /////

	// no remapping, no delays
	tty.c_oflag = 0;
	
	//// CONTROL CHARACTERS /////

	// read doesn't block
	tty.c_cc[VMIN]  = 0;
	
	// 0.5 seconds read timeout
	tty.c_cc[VTIME] = 0;
	
	RLOG("CFLAGS : " << tty.c_cflag);
	RLOG("LFLAGS : " << tty.c_lflag);
	RLOG("IFLAGS : " << tty.c_iflag);
	RLOG("OFLAGS : " << tty.c_oflag);
	
	if (tcsetattr(fd, TCSANOW, &tty) != 0)
	{
		Close();
		isError = true;
		errCode = DeviceError;
		return false;
	}
	
	return true;
}

// close the port
void Serial::Close(void)
{
	if(fd != -1)
		close(fd);
	fd = -1;
	isError = false;
	errCode = Ok;
}

// flush data
bool Serial::FlushInput(void)
{
	if(fd == -1)
		return false;
	return !tcflush(fd, TCIFLUSH);
}

bool Serial::FlushOutput(void)
{
	if(fd == -1)
		return false;
	return !tcflush(fd, TCOFLUSH);
}

bool Serial::FlushAll(void)
{
	if(fd == -1)
		return false;
	return !tcflush(fd, TCIOFLUSH);
}
		
// read a single byte, block 'timeout' milliseconds
bool Serial::Read(byte &c, uint32_t timeout)
{
	char buf[1];
	
	uint32_t tim = msecs() + timeout;
	for(;;)
	{
		if(!read(fd, buf, 1))
		{
			if((uint32_t)msecs() > tim)
				return false;
			continue;
		}
		c = (byte)buf[0];
		return true;
	}		
}

bool Serial::Read(char &c, uint32_t timeout)
{
	char buf[1];
	
	uint32_t tim = msecs() + timeout;
	for(;;)
	{
		if(!read(fd, buf, 1))
		{
			if((uint32_t)msecs() > tim)
				return false;
			continue;
		}
		c = buf[0];
		return true;
	}		
}

// write a single byte
bool Serial::Write(char c, uint32_t timeout)
{
	if(!timeout)
		return write(fd, &c, 1);

	uint32_t tim = msecs() + timeout;
	for(;;)
	{
		if(write(fd, &c, 1) == 1)
			return true;
		if((uint32_t)msecs() > tim)
			break;
	}
	
	return false;
}
		
// read data, requested amount, blocks 'timeout' milliseconds
// if reqSize == 0 just read all available data, waiting for 'timeout' if != 0
String Serial::Read(size_t reqSize, uint32_t timeout)
{
	char buf[1001];
	buf[1000] = 0;
	String data;

	size_t n;
	uint32_t tim = msecs() + timeout;
	if(reqSize)
	{
		n = min(reqSize, (size_t)1000);
		while(reqSize)
		{
			n = read(fd, buf, n);
			if(!n)
			{
				if((uint32_t)msecs() > tim)
					break;
				n = min(reqSize, (size_t)1000);
				continue;
			}
			tim = msecs() + timeout;
			if(n)
			{
				reqSize -= n;
				data.Cat(buf, n);
			}
			n = min(reqSize, (size_t)1000);
		}
	}
	else
	{
		for(;;)
		{
			n = read(fd, buf, 1000);
			if(!n)
			{
				if((uint32_t)msecs() > tim)
					break;
				continue;
			}
			tim = msecs() + timeout;
			if(n)
				data.Cat(buf, n);
		}
	}
	return data;	
}

// writes data
bool Serial::Write(String const &data, uint32_t timeout)
{
	int dLen = data.GetCount();
	if(!timeout)
		return (write(fd, ~data, data.GetCount()) == dLen);

	uint32_t tim = msecs() + timeout;
	const char *dPos = ~data;
	for(;;)
	{
		int written = write(fd, dPos, dLen);
		if(written == dLen)
			return true;
		if(written >= 0)
		{
			dPos += written;
			dLen -= written;
			continue;
		}
		if((uint32_t)msecs() >= tim)
			break;
	}
	return false;
}

// check if opened
bool Serial::IsOpened(void) const
{
	return fd != -1;
}

// get a list of connected serial ports
// for old-style serials, check if something is connected
// for usb ones, return also a description
ArrayMap<String, String> Serial::GetSerialPorts(void)
{
	const char *sysClassTTY = "/sys/class/tty/";

	ArrayMap<String, String> res;

	// first, scan all tty devices and dereference symlinks
	FindFile ff(AppendFileName(sysClassTTY, "*"));

	while (ff)
	{
		if (ff.IsSymLink())
		{
			// it's a symlink
			// get the device name
			String devName = ff.GetName();

			// build the full path of it
			String serial = AppendFileName(sysClassTTY, devName);

			// look for its driver, skip if none
			String devPath = AppendFileName(serial, "device");

			// it must be a symlink....

			if (__IsSymLink(devPath))
			{
				// get the driver path
				String drvLink = AppendFileName(devPath, "driver");

				// it must be a symlink too...

				if (__IsSymLink(drvLink))
				{
					// get link target
					String drvName = GetFileName(GetSymLinkPath(drvLink));

					if (!drvName.IsEmpty())
					{
						// build the full device name
						devName = AppendFileName("/dev", devName);

						// now if driver is 'serial8250' we check if
						// the port responds

						if (drvName == "serial8250")
						{
							// Try to open the device
							int fd = open(devName, O_RDWR | O_NONBLOCK | O_NOCTTY);

							if (fd >= 0)
							{
								// Get serial_info

								struct serial_struct serinfo;

								if (ioctl(fd, TIOCGSERIAL, &serinfo) == 0)
								{
									// If device type is no PORT_UNKNOWN we accept the port
									if (serinfo.type != PORT_UNKNOWN)
										res.Add(devName, "");
								}

								close(fd);
							}
						}

						// otherwise, just add the device to result

						else
							res.Add(devName, drvName);
					}
				}
			}
		}

		ff.Next();
	}

	return res;
}

END_UPP_NAMESPACE

#endif
