/*------------------------------------------------------------------------------

   Copyright (c) 2000 Tyrell Corporation. All rights reserved.

   Tyrell DarkIce

   File     : DarkIce.cpp
   Version  : $Revision$
   Author   : $Author$
   Location : $Source$
   

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
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

------------------------------------------------------------------------------*/

/* ============================================================ include files */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
#error need stdlib.h
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#else
#error need unistd.h
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#else
#error need sys/types.h
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#else
#error need sys/wait.h
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#else
#error need errno.h
#endif

#ifdef HAVE_SCHED_H
#include <sched.h>
#else
#error need sched.h
#endif


#include <hash_map>
#include <string>


#include "Util.h"
#include "DarkIce.h"


/* ===================================================  local data structures */


/* ================================================  local constants & macros */

/*------------------------------------------------------------------------------
 *  File identity
 *----------------------------------------------------------------------------*/
static const char fileid[] = "$Id$";


/*------------------------------------------------------------------------------
 *  Make sure wait-related stuff is what we expect
 *----------------------------------------------------------------------------*/
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val)      ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val)        (((stat_val) & 255) == 0)
#endif



/* ===============================================  local function prototypes */


/* =============================================================  module code */

/*------------------------------------------------------------------------------
 *  Initialize the object
 *----------------------------------------------------------------------------*/
void
DarkIce :: init ( const Config      & config )              throw ( Exception )
{
    unsigned int             bufferSecs;
    const ConfigSection    * cs;
    const char             * str;
    unsigned int             sampleRate;
    unsigned int             bitsPerSample;
    unsigned int             channel;
    const char             * device;

    // the [general] section
    if ( !(cs = config.get( "general")) ) {
        throw Exception( __FILE__, __LINE__, "no section [general] in config");
    }
    str = cs->getForSure( "duration", " missing in section [general]");
    duration = Util::strToL( str);
    str = cs->getForSure( "bufferSecs", " missing in section [general]");
    bufferSecs = Util::strToL( str);


    // the [input] section
    if ( !(cs = config.get( "input")) ) {
        throw Exception( __FILE__, __LINE__, "no section [general] in config");
    }
    
    str = cs->getForSure( "sampleRate", " missing in section [input]");
    sampleRate = Util::strToL( str);
    str = cs->getForSure( "bitsPerSample", " missing in section [input]");
    bitsPerSample = Util::strToL( str);
    str = cs->getForSure( "channel", " missing in section [input]");
    channel = Util::strToL( str);
    device = cs->getForSure( "device", " missing in section [input]");

    dsp             = new OssDspSource( device,
                                        sampleRate,
                                        bitsPerSample,
                                        channel );
    encConnector    = new Connector( dsp.get());

    configLameLib( config, bufferSecs);
}


/*------------------------------------------------------------------------------
 *  Look for the lame library outputs from the config file.
 *----------------------------------------------------------------------------*/
void
DarkIce :: configLameLib (  const Config      & config,
                            unsigned int        bufferSecs  )
                                                        throw ( Exception )
{
    // look for lame encoder output streams, sections [lamelib0], [lamelib1]...
    char            lame[]          = "lame ";
    size_t          lameLen         = Util::strLen( lame);
    unsigned int    u;

    for ( u = 0; u < maxOutput; ++u ) {
        const ConfigSection    * cs;
        const char             * str;

        // ugly hack to change the section name to "lame0", "lame1", etc.
        lame[lameLen-1] = '0' + u;

        if ( !(cs = config.get( lame)) ) {
            break;
        }

        unsigned int    bitrate         = 0;
        const char    * server          = 0;
        unsigned int    port            = 0;
        const char    * password        = 0;
        const char    * mountPoint      = 0;
        const char    * remoteDumpFile  = 0;
        const char    * name            = 0;
        const char    * description     = 0;
        const char    * url             = 0;
        const char    * genre           = 0;
        bool            isPublic        = false;
        unsigned int    lowpass         = 0;
        unsigned int    highpass        = 0;

        str         = cs->getForSure( "bitrate", " missing in section ", lame);
        bitrate     = Util::strToL( str);
        server      = cs->getForSure( "server", " missing in section ", lame);
        str         = cs->getForSure( "port", " missing in section ", lame);
        port        = Util::strToL( str);
        password    = cs->getForSure( "password", " missing in section ", lame);
        mountPoint  = cs->getForSure( "mountPoint"," missing in section ",lame);
        remoteDumpFile = cs->get( "remoteDumpFile");
        name        = cs->get( "name");
        description = cs->get("description");
        url         = cs->get( "url");
        genre       = cs->get( "genre");
        str         = cs->get( "public");
        isPublic    = str ? (Util::strEq( str, "yes") ? true : false) : false;
        str         = cs->get( "lowpass");
        lowpass     = str ? Util::strToL( str) : 0;
        str         = cs->get( "highpass");
        highpass    = str ? Util::strToL( str) : 0;

        // go on and create the things

        // encoder related stuff
        unsigned int bs = bufferSecs *
                          (dsp->getBitsPerSample() / 8) *
                          dsp->getChannel() *
                          dsp->getSampleRate();
        reportEvent( 6, "using buffer size", bs);

        // streaming related stuff
        lameLibOuts[u].socket     = new TcpSocket( server, port);
        lameLibOuts[u].ice        = new IceCast( lameLibOuts[u].socket.get(),
                                                 password,
                                                 mountPoint,
                                                 bitrate,
                                                 name,
                                                 description,
                                                 url,
                                                 genre,
                                                 isPublic,
                                                 remoteDumpFile );

        lameLibOuts[u].encoder = new LameLibEncoder( lameLibOuts[u].ice.get(),
                                                     dsp.get(),
                                                     bitrate,
                                                     dsp->getSampleRate(),
                                                     dsp->getChannel(),
                                                     lowpass,
                                                     highpass );

        encConnector->attach( lameLibOuts[u].encoder.get());
    }

    noLameLibOuts = u;
}


/*------------------------------------------------------------------------------
 *  Set POSIX real-time scheduling, if super-user
 *----------------------------------------------------------------------------*/
void
DarkIce :: setRealTimeScheduling ( void )               throw ( Exception )
{
    uid_t   euid;

    euid = geteuid();

    if ( euid == 0 ) {
        int                 high_priority;
        struct sched_param  param;

        /* store the old scheduling parameters */
        if ( (origSchedPolicy = sched_getscheduler(0)) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getscheduler", errno);
        }

        if ( sched_getparam( 0, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getparam", errno);
        }
        origSchedPriority = param.sched_priority;
        
        /* set SCHED_FIFO with max - 1 priority */
        if ( (high_priority = sched_get_priority_max(SCHED_FIFO)) == -1 ) {
            throw Exception(__FILE__,__LINE__,"sched_get_priority_max",errno);
        }
        reportEvent( 8, "scheduler high priority", high_priority);

        param.sched_priority = high_priority - 1;

        if ( sched_setscheduler( 0, SCHED_FIFO, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_setscheduler", errno);
        }

        /* ask the new priortiy and report it */
        if ( sched_getparam( 0, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getparam", errno);
        }

        reportEvent( 1,
                     "Using POSIX real-time scheduling, priority",
                     param.sched_priority );
    } else {
        reportEvent( 1,
        "Not running as super-user, unable to use POSIX real-time scheduling" );
        reportEvent( 1,
        "It is recommended that you run this program as super-user");
    }
}


/*------------------------------------------------------------------------------
 *  Set the original scheduling of the process, the one prior to the
 *  setRealTimeScheduling call.
 *  WARNING: make sure you don't call this before setRealTimeScheduling!!
 *----------------------------------------------------------------------------*/
void
DarkIce :: setOriginalScheduling ( void )               throw ( Exception )
{
    uid_t   euid;

    euid = geteuid();

    if ( euid == 0 ) {
        struct sched_param  param;

        if ( sched_getparam( 0, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_getparam", errno);
        }

        param.sched_priority = origSchedPriority;

        if ( sched_setscheduler( 0, origSchedPolicy, &param) == -1 ) {
            throw Exception( __FILE__, __LINE__, "sched_setscheduler", errno);
        }

        reportEvent( 5, "reverted to original scheduling");
    }
}


/*------------------------------------------------------------------------------
 *  Run the encoder
 *----------------------------------------------------------------------------*/
bool
DarkIce :: encode ( void )                          throw ( Exception )
{
    unsigned int       len;
    unsigned long      bytes;

    if ( !encConnector->open() ) {
        throw Exception( __FILE__, __LINE__, "can't open connector");
    }
    
    bytes = dsp->getSampleRate() *
            (dsp->getBitsPerSample() / 8UL) *
            dsp->getChannel() *
            duration;
                                                
    len = encConnector->transfer( bytes, 4096, 1, 0 );

    reportEvent( 1, len, "bytes transfered to the encoders");

    encConnector->close();

    return true;
}


/*------------------------------------------------------------------------------
 *  Run
 *----------------------------------------------------------------------------*/
int
DarkIce :: run ( void )                             throw ( Exception )
{
    reportEvent( 3, "encoding");
    setRealTimeScheduling();
    encode();
    setOriginalScheduling();
    reportEvent( 3, "encoding ends");

    return 0;
}


/*------------------------------------------------------------------------------
 
  $Source$

  $Log$
  Revision 1.15  2001/08/30 17:25:56  darkeye
  renamed configure.h to config.h

  Revision 1.14  2001/08/29 21:08:30  darkeye
  made some description options in the darkice config file optional

  Revision 1.13  2001/08/26 20:44:30  darkeye
  removed external command-line encoder support
  replaced it with a shared-object support for lame with the possibility
  of static linkage

  Revision 1.12  2000/12/20 12:36:47  darkeye
  added POSIX real-time scheduling

  Revision 1.11  2000/11/18 11:13:27  darkeye
  removed direct reference to cout, except from main.cpp
  all class use the Reporter interface to report events

  Revision 1.10  2000/11/17 15:50:48  darkeye
  added -Wall flag to compiler and eleminated new warnings

  Revision 1.9  2000/11/15 18:37:37  darkeye
  changed the transferable number of bytes to unsigned long

  Revision 1.8  2000/11/15 18:08:43  darkeye
  added multiple verbosity-level event reporting and verbosity command
  line option

  Revision 1.7  2000/11/13 19:38:55  darkeye
  moved command line parameter parsing from DarkIce.cpp to main.cpp

  Revision 1.6  2000/11/13 18:46:50  darkeye
  added kdoc-style documentation comments

  Revision 1.5  2000/11/10 20:16:21  darkeye
  first real tests with multiple streaming

  Revision 1.4  2000/11/09 22:09:46  darkeye
  added multiple outputs
  added configuration reading
  added command line processing

  Revision 1.3  2000/11/08 17:29:50  darkeye
  added configuration file reader

  Revision 1.2  2000/11/05 14:08:27  darkeye
  changed builting to an automake / autoconf environment

  Revision 1.1.1.1  2000/11/05 10:05:49  darkeye
  initial version

  
------------------------------------------------------------------------------*/

