/*------------------------------------------------------------------------------

   Copyright (c) 2000 Tyrell Corporation. All rights reserved.

   Tyrell DarkIce

   File     : TcpSocket.cpp
   Version  : $Revision$
   Author   : $Author$
   Location : $Source$
   
   Abstract : 

     A TCP network socket

   Copyright notice:

     This program is free software; you can redistribute it and/or
     modify it under the terms of the GNU General Public License  
     as published by the Free Software Foundation; either version 2
     of the License, or (at your option) any later version.
    
     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of 
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
     GNU General Public License for more details.
    
     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
     USA.

------------------------------------------------------------------------------*/

/* ============================================================ include files */

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include "Util.h"
#include "Exception.h"
#include "TcpSocket.h"


/* ===================================================  local data structures */


/* ================================================  local constants & macros */

/*------------------------------------------------------------------------------
 *  File identity
 *----------------------------------------------------------------------------*/
static const char fileid[] = "$Id$";


/* ===============================================  local function prototypes */


/* =============================================================  module code */

/*------------------------------------------------------------------------------
 *  Initialize the object
 *----------------------------------------------------------------------------*/
void
TcpSocket :: init (   const char    * host,
                      unsigned short  port )          throw ( Exception )
{
    this->host   = Util::strDup( host);
    this->port   = port;
    this->sockfd = 0;
}


/*------------------------------------------------------------------------------
 *  De-initialize the object
 *----------------------------------------------------------------------------*/
void
TcpSocket :: strip ( void)                           throw ( Exception )
{
    if ( isOpen() ) {
        close();
    }

    delete[] host;
}


/*------------------------------------------------------------------------------
 *  Copy Constructor
 *----------------------------------------------------------------------------*/
TcpSocket :: TcpSocket (  const TcpSocket &    ss )    throw ( Exception )
                : Sink( ss ), Source( ss )
{
    int     fd;
    
    init( ss.host, ss.port);

    if ( (fd = ss.sockfd ? dup( ss.sockfd) : 0) == -1 ) {
        strip();
        throw Exception( __FILE__, __LINE__, "dup failure");
    }

    sockfd = fd;
}


/*------------------------------------------------------------------------------
 *  Assignment operator
 *----------------------------------------------------------------------------*/
TcpSocket &
TcpSocket :: operator= (  const TcpSocket &    ss )   throw ( Exception )
{
    if ( this != &ss ) {
        int     fd;

        /* first strip */
        strip();


        /* then build up */
        Sink::operator=( ss );
        Source::operator=( ss );

        init( ss.host, ss.port);
        
        if ( (fd = ss.sockfd ? dup( ss.sockfd) : 0) == -1 ) {
            strip();
            throw Exception( __FILE__, __LINE__, "dup failure");
        }

        sockfd = fd;
    }

    return *this;
}


/*------------------------------------------------------------------------------
 *  Open the file
 *----------------------------------------------------------------------------*/
bool
TcpSocket :: open ( void )                       throw ( Exception )
{
    struct sockaddr_in      addr;
    struct hostent        * pHostEntry;
    
    if ( isOpen() ) {
        return false;
    }

    if ( !(pHostEntry = gethostbyname( host)) ) {
        throw Exception( __FILE__, __LINE__, "gethostbyname error", errno);
    }
    
    if ( (sockfd = socket( AF_INET, SOCK_STREAM,  IPPROTO_TCP)) == -1 ) {
        throw Exception( __FILE__, __LINE__, "socket error", errno);
    }

    memset( &addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = *((long*) pHostEntry->h_addr_list[0]);

    if ( connect( sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1 ) {
        ::close( sockfd);
        throw Exception( __FILE__, __LINE__, "connect error", errno);
    }

    return true;
}


/*------------------------------------------------------------------------------
 *  Check wether read() would return anything
 *----------------------------------------------------------------------------*/
bool
TcpSocket :: canRead (      unsigned int    sec,
                            unsigned int    usec )      throw ( Exception )
{
    fd_set              fdset;
    struct timeval      tv;
    int                 ret;

    if ( !isOpen() ) {
        return false;
    }

    FD_ZERO( &fdset);
    FD_SET( sockfd, &fdset);
    tv.tv_sec  = sec;
    tv.tv_usec = usec;

    ret = select( sockfd + 1, &fdset, NULL, NULL, &tv);
    
    if ( ret == -1 ) {
        throw Exception( __FILE__, __LINE__, "select error");
    }

    return ret > 0;
}


/*------------------------------------------------------------------------------
 *  Read from the socket
 *----------------------------------------------------------------------------*/
unsigned int
TcpSocket :: read (     void          * buf,
                        unsigned int    len )       throw ( Exception )
{
    int         ret;

    if ( !isOpen() ) {
        return 0;
    }

    ret = recv( sockfd, buf, len, 0);

    if ( ret == -1 ) {
        throw Exception( __FILE__, __LINE__, "recv error", errno);
    }

    return ret;
}


/*------------------------------------------------------------------------------
 *  Check wether read() would return anything
 *----------------------------------------------------------------------------*/
bool
TcpSocket :: canWrite (    unsigned int    sec,
                            unsigned int    usec )      throw ( Exception )
{
    fd_set              fdset;
    struct timeval      tv;
    int                 ret;

    if ( !isOpen() ) {
        return false;
    }

    FD_ZERO( &fdset);
    FD_SET( sockfd, &fdset);
    tv.tv_sec  = sec;
    tv.tv_usec = usec;

    ret = select( sockfd + 1, NULL, &fdset, NULL, &tv);
    
    if ( ret == -1 ) {
        throw Exception( __FILE__, __LINE__, "select error");
    }

    return ret > 0;
}


/*------------------------------------------------------------------------------
 *  Write to the socket
 *----------------------------------------------------------------------------*/
unsigned int
TcpSocket :: write (    const void    * buf,
                        unsigned int    len )       throw ( Exception )
{
    int         ret;

    if ( !isOpen() ) {
        return 0;
    }

//    ret = send( sockfd, buf, len, MSG_DONTWAIT);
    ret = send( sockfd, buf, len, 0);

    if ( ret == -1 ) {
        if ( errno == EAGAIN ) {
            ret = 0;
        } else {
            throw Exception( __FILE__, __LINE__, "send error", errno);
        }
    }

    return ret;
}


/*------------------------------------------------------------------------------
 *  Close the socket
 *----------------------------------------------------------------------------*/
void
TcpSocket :: close ( void )                          throw ( Exception )
{
    if ( !isOpen() ) {
        return;
    }

    flush();
    ::close( sockfd);
    sockfd = 0;
}



/*------------------------------------------------------------------------------
 
  $Source$

  $Log$
  Revision 1.1  2000/11/05 10:05:55  darkeye
  Initial revision

  
------------------------------------------------------------------------------*/

