//
// $Id$
//
// verify connection state prior to doing anything in this file
//
//
// $Log$
// Revision 1.4  1996/07/24 22:03:36  jhill
// fixed net proto for gnu compiler
//
// Revision 1.3  1996/07/09 22:55:22  jhill
// added cast
//
// Revision 1.2  1996/06/21 02:18:11  jhill
// SOLARIS port
//
// Revision 1.1.1.1  1996/06/20 00:28:19  jhill
// ca server installation
//
//

#include <server.h>
#ifdef SOLARIS
#       include <sys/filio.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
//
// the SUNOS4 and vxWorks5.2 prototypes are trad C 
//
#if !defined(SUNOS4) && !defined(vxWorks) 
#include <arpa/inet.h>
#else
extern "C" {
char * inet_ntoa(struct in_addr);
}
#endif


//
// casStreamIO::casStreamIO()
//
casStreamIO::casStreamIO(const SOCKET s, const caAddr &a) :
	sockState(casOffLine), sock(s), addr(a) 
{
	assert (sock>=0);
}


//
// casStreamIO::init()
//
caStatus casStreamIO::init() 
{
	int 	yes = TRUE;
	int	status;

        /*
         * see TCP(4P) this seems to make unsollicited single events much
         * faster. I take care of queue up as load increases.
         */
        status = setsockopt(
                                this->sock,
                                IPPROTO_TCP,
                                TCP_NODELAY,
                                (char *)&yes,
                                sizeof(yes));
        if (status<0) {
		ca_printf(
			"CAS: %s TCP_NODELAY option set failed %s\n",
			__FILE__,
			strerror(SOCKERRNO));
		return S_cas_internal;
        }

        /*
         * turn on KEEPALIVE so if the client crashes
         * this task will find out and exit
         */
        status = setsockopt(
                        sock,
                        SOL_SOCKET,
                        SO_KEEPALIVE,
                        (char *)&yes,
                        sizeof(yes));
        if (status<0) {
		ca_printf(
			"CAS: %s SO_KEEPALIVE option set failed %s\n",
			__FILE__,
			strerror(SOCKERRNO));
		return S_cas_internal;
        }
#ifdef MATCHING_BUFFER_SIZES
	int i;

        /*
         * set TCP buffer sizes to be synergistic
         * with CA internal buffering
         */
        i = MAX_MSG_SIZE;
        status = setsockopt(
                        sock,
                        SOL_SOCKET,
                        SO_SNDBUF,
                        (char *)&i,
                        sizeof(i));
        if(status < 0){
                ca_printf("CAS: SO_SNDBUF set failed\n");
		return S_cas_internal;
        }
        i = MAX_MSG_SIZE;
        status = setsockopt(
                        sock,
                        SOL_SOCKET,
                        SO_RCVBUF,
                        (char *)&i,
                        sizeof(i));
        if(status < 0){
                ca_printf("CAS: SO_RCVBUF set failed\n");
		return S_cas_internal;
        }
#endif
	this->sockState = casOnLine;

	return S_cas_success;
}


//
// casStreamIO::~casStreamIO()
//
casStreamIO::~casStreamIO()
{
	if (sock>=0) {
		close(this->sock);
printf("closing sock=%d\n", this->sock);
	}
}


//
// casStreamIO::osdSend()
//
xSendStatus casStreamIO::osdSend(const char *pBuf, bufSizeT nBytesReq, 
		bufSizeT &nBytesActual)
{
        int	status;

#if 0
        if(this->pCAS->debugLevel>2u){
                char    buf[64];

                (*pCAS->osm.hostName) (
                        &this->addr,
                        buf,
                        sizeof(buf));
                ca_printf(
                        "CAS: Sending a %d byte reply to %s\n",
                        this->pSend->stk,
                        buf);
        }
#endif

        if (this->sockState!=casOnLine) {
                return xSendDisconnect;
        }

        if (nBytesReq<=0u) {
		nBytesActual = 0u;
                return xSendOK;
        }

	status = send (
			this->sock,
			(char *) pBuf,
			nBytesReq,
			0);
	if (status == 0) {
		this->sockState = casOffLine;
		return xSendDisconnect;
	}
	else if (status<0) {
		int anerrno = SOCKERRNO;

		if (anerrno != EWOULDBLOCK) {
			this->sockState = casOffLine;
		}
		nBytesActual = 0u;
		return xSendOK;
	}
	nBytesActual = (bufSizeT) status;
	return xSendOK;
}


//
// casStreamIO::osdRecv()
//
xRecvStatus casStreamIO::osdRecv(char *pBuf, bufSizeT nBytes, 
		bufSizeT &nBytesActual)
{
  	int 		nchars;

	if (this->sockState!= casOnLine) {
		return xRecvDisconnect;
	}

	nchars = recv(this->sock, pBuf, nBytes, 0);
	if (nchars==0) {
		this->sockState = casOffLine;
		return xRecvDisconnect;
	}
	else if (nchars<0) {
		/*
		 * normal conn lost conditions
		 */
		switch(SOCKERRNO){
		case EWOULDBLOCK:
			nBytesActual = 0u;
			return xRecvOK;

		case ECONNABORTED:
		case ECONNRESET:
		case ETIMEDOUT:
			break;

		default:
			ca_printf(
		"CAS: client disconnect because \"%s\"\n",
				strerror(SOCKERRNO));
			break;
		}
		this->sockState = casOffLine;
		return xRecvDisconnect;
	}
	nBytesActual = (bufSizeT) nchars;
	return xRecvOK;
}


//
// casStreamIO::show()
//
void casStreamIO::osdShow (unsigned level) const
{
	printf ("casStreamIO at %x\n", (unsigned) this);
	if (level>1u) {
		printf (
			"client address=%x, port=%x\n",
			(unsigned) ntohl(this->addr.in.sin_addr.s_addr),
			ntohs(this->addr.in.sin_port));
	}
}


//
// casStreamIO::xSsetNonBlocking()
//
void casStreamIO::xSetNonBlocking()
{
	int status;
	int yes = TRUE;

        if (this->sockState!=casOnLine) {
                return;
        }

	status = socket_ioctl(this->sock, FIONBIO, &yes);
	if (status<0) {
		ca_printf("%s:CAS: TCP non blocking IO set fail because \"%s\"\n", 
				__FILE__, strerror(SOCKERRNO));
		this->sockState = casOffLine;
	}
}


//
// casStreamIO::incommingBytesPresent()
//
bufSizeT casStreamIO::incommingBytesPresent() const
{
	int status;
	int nchars;

	status = socket_ioctl(this->sock, FIONREAD, &nchars);
	if (status<0) {
		ca_printf("CAS: FIONREAD err %s\n", strerror(SOCKERRNO));
		return 0u;
	}
	else if (nchars<0) {
		return 0u;
	}
	else {
		return (bufSizeT) status;
	}
}


//
// casStreamIO::hostName()
//
void casStreamIO::hostNameFromAddr(char *pBuf, unsigned bufSize)
{
	hostNameFromIPAddr(&this->addr, pBuf, bufSize);
}


//
// hostNameFromIPAddr()
//
void hostNameFromIPAddr (const caAddr *pAddr, 
                char *pBuf, unsigned bufSize)
{
        char            *pName;

        assert (bufSize>0U);

        if (pAddr->sa.sa_family != AF_INET) {
                strncpy (pBuf, "UKN ADDR FAMILY", bufSize);
        }
        else {
                int port = ntohs(pAddr->in.sin_port);
#               define maxPortDigits 15U
                char tmp[maxPortDigits+1];
                unsigned size;

#if 0 // bypass this because of problems with SUNOS4 header files and C++
        	struct hostent  *ent;

                ent = gethostbyaddr(
                        (char *) &pAddr->in.sin_addr.s_addr,
                        sizeof(pAddr->in.sin_addr.s_addr),
                        AF_INET);
                if (ent) {
                        pName = ent->h_name;
                }
                else {
                        pName = inet_ntoa (pAddr->in.sin_addr);
                }
#else
		pName = inet_ntoa (pAddr->in.sin_addr);
#endif
		
                assert (bufSize>maxPortDigits);
                size = bufSize - maxPortDigits;
                strncpy (pBuf, pName, size);
                pBuf[size] = '\0';
                sprintf (tmp, ".%d", port);
                strncat (pBuf, tmp, maxPortDigits);
        }
        pBuf[bufSize-1] = '\0';
        assert(strlen(pBuf)<=bufSize);
}

//
// casStreamIO:::optimumBufferSize()
//
bufSizeT casStreamIO::optimumBufferSize ()
{

        if (this->sockState!=casOnLine) {
                return 0x400;
        }

#if 0
	int n;
	int size;
	int status;

	/* fetch the TCP send buffer size */
	n = sizeof(size);
	status = getsockopt(
			this->sock,
			SOL_SOCKET,
			SO_SNDBUF,
			(char *)&size,
			&n);
	if(status < 0 || n != sizeof(size)){
		size = 0x400;
	}

	if (size<=0) {
		size = 0x400;
	}
printf("the tcp buf size is %d\n", size);
	return (bufSizeT) size;
#else
// this needs to be MAX_TCP (until we fix the array problem)
	return (bufSizeT) MAX_TCP;
#endif
}

//
// casStreamIO::getFileDescriptor()
//
int casStreamIO::getFileDescriptor() const
{
	return sock;
}


//
// casStreamIO::state()
//
casIOState casStreamIO::state() const 
{
	return this->sockState;
}

