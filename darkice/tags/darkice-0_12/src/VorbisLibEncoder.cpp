/*------------------------------------------------------------------------------

   Copyright (c) 2000 Tyrell Corporation. All rights reserved.

   Tyrell DarkIce

   File     : VorbisLibEncoder.cpp
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

// compile only if configured for Ogg Vorbis
#ifdef HAVE_VORBIS_LIB


#include "Exception.h"
#include "Util.h"
#include "VorbisLibEncoder.h"


/* ===================================================  local data structures */


/* ================================================  local constants & macros */

/*------------------------------------------------------------------------------
 *  File identity
 *----------------------------------------------------------------------------*/
static const char fileid[] = "$Id$";


/* ===============================================  local function prototypes */


/* =============================================================  module code */

/*------------------------------------------------------------------------------
 *  Initialize the encoder
 *----------------------------------------------------------------------------*/
void
VorbisLibEncoder :: init ( Sink           * sink,
                           unsigned int     outMaxBitrate )
                                                            throw ( Exception )
{
    this->sink          = sink;
    this->outMaxBitrate = outMaxBitrate;

    if ( getInBitsPerSample() != 16 && getInBitsPerSample() != 8 ) {
        throw Exception( __FILE__, __LINE__,
                         "specified bits per sample not supported",
                         getInBitsPerSample() );
    }

    if ( getInChannel() != 1 && getInChannel() != 2 ) {
        throw Exception( __FILE__, __LINE__,
                         "unsupported number of channels for the encoder",
                         getInChannel() );
    }
    if ( getInChannel() != getOutChannel() ) {
        throw Exception( __FILE__, __LINE__,
                         "different number of input and output channels",
                         getOutChannel() );
    }

    if ( getOutSampleRate() == getInSampleRate() ) {
        resampleRatio = 1;
        converter     = 0;
    } else {
        resampleRatio = ( (double) getOutSampleRate() /
                          (double) getInSampleRate() );
        // open the aflibConverter in
        // - high quality
        // - not linear (quadratic) interpolation
        // - not filter interpolation
        converter = new aflibConverter( true, true, false);
    }

    if ( getInChannel() != getOutChannel() ) {
        throw Exception( __FILE__, __LINE__,
                         "different in and out channels not supported");
    }

    encoderOpen = false;
}


/*------------------------------------------------------------------------------
 *  Open an encoding session
 *----------------------------------------------------------------------------*/
bool
VorbisLibEncoder :: open ( void )
                                                            throw ( Exception )
{
    int     ret;

    if ( isOpen() ) {
        close();
    }

    // open the underlying sink
    if ( !sink->open() ) {
        throw Exception( __FILE__, __LINE__,
                         "vorbis lib opening underlying sink error");
    }

    vorbis_info_init( &vorbisInfo);

    switch ( getOutBitrateMode() ) {

        case cbr: {
                int     maxBitrate = getOutMaxBitrate() * 1000;
                if ( !maxBitrate ) {
                    maxBitrate = -1;
                }
                if ( (ret = vorbis_encode_init( &vorbisInfo,
                                                getInChannel(),
                                                getOutSampleRate(),
                                                maxBitrate,
                                                getOutBitrate() * 1000,
                                                -1)) ) {
                    throw Exception( __FILE__, __LINE__,
                                     "vorbis encode init error", ret);
                }
            } break;

        case abr:
            /* set non-managed VBR around the average bitrate */
            ret = vorbis_encode_setup_managed( &vorbisInfo,
                                               getInChannel(),
                                               getOutSampleRate(),
                                               -1,
                                               getOutBitrate() * 1000,
                                               -1 )
               || vorbis_encode_ctl( &vorbisInfo, OV_ECTL_RATEMANAGE_SET, NULL)
               || vorbis_encode_setup_init( &vorbisInfo);
            if ( ret ) {
                throw Exception( __FILE__, __LINE__,
                                 "vorbis encode init error", ret);
            }
            break;

        case vbr:
            if ( (ret = vorbis_encode_init_vbr( &vorbisInfo,
                                                getInChannel(),
                                                getOutSampleRate(),
                                                getOutQuality() )) ) {
                throw Exception( __FILE__, __LINE__,
                                 "vorbis encode init error", ret);
            }
            break;
    }

    if ( (ret = vorbis_analysis_init( &vorbisDspState, &vorbisInfo)) ) {
        throw Exception( __FILE__, __LINE__, "vorbis analysis init error", ret);
    }

    if ( (ret = vorbis_block_init( &vorbisDspState, &vorbisBlock)) ) {
        throw Exception( __FILE__, __LINE__, "vorbis block init error", ret);
    }

    if ( (ret = ogg_stream_init( &oggStreamState, 0)) ) {
        throw Exception( __FILE__, __LINE__, "ogg stream init error", ret);
    }

    // create an empty vorbis_comment structure
    vorbis_comment_init( &vorbisComment);

    // create the vorbis stream headers and send them to the underlying sink
    ogg_packet      header;
    ogg_packet      commentHeader;
    ogg_packet      codeHeader;

    if ( (ret = vorbis_analysis_headerout( &vorbisDspState,
                                           &vorbisComment,
                                           &header,
                                           &commentHeader,
                                           &codeHeader )) ) {
        throw Exception( __FILE__, __LINE__, "vorbis header init error", ret);
    }

    ogg_stream_packetin( &oggStreamState, &header);
    ogg_stream_packetin( &oggStreamState, &commentHeader);
    ogg_stream_packetin( &oggStreamState, &codeHeader);

    ogg_page        oggPage;
    while ( ogg_stream_flush( &oggStreamState, &oggPage) ) {
        sink->write( oggPage.header, oggPage.header_len);
        sink->write( oggPage.body, oggPage.body_len);
    }

    vorbis_comment_clear( &vorbisComment );

    // initialize the resampling coverter if needed
    if ( converter ) {
        converter->initialize( resampleRatio, getInChannel());
    }

    encoderOpen = true;

    return true;
}


/*------------------------------------------------------------------------------
 *  Write data to the encoder
 *----------------------------------------------------------------------------*/
unsigned int
VorbisLibEncoder :: write ( const void    * buf,
                            unsigned int    len )           throw ( Exception )
{
    if ( !isOpen() || len == 0 ) {
        return 0;
    }

    unsigned int    bitsPerSample = getInBitsPerSample();
    unsigned int    channels      = getInChannel();

    unsigned int    sampleSize = (bitsPerSample / 8) * channels;
    unsigned char * b = (unsigned char*) buf;
    unsigned int    processed = len - (len % sampleSize);
    unsigned int    nSamples = processed / sampleSize;
    float        ** vorbisBuffer;


    // convert the byte-based raw input into a short buffer
    // with channels still interleaved
    unsigned int    totalSamples = nSamples * channels;
    short int     * shortBuffer  = new short int[totalSamples];
    Util::conv( bitsPerSample, b, processed, shortBuffer, isInBigEndian());
    
    if ( converter ) {
        // resample if needed
        int         inCount  = nSamples;
        int         outCount = (int) (inCount * resampleRatio);
        short int * resampledBuffer = new short int[outCount * channels];
        int         converted;

        converted = converter->resample( inCount,
                                         outCount,
                                         shortBuffer,
                                         resampledBuffer );

        vorbisBuffer = vorbis_analysis_buffer( &vorbisDspState,
                                               converted);
        Util::conv( resampledBuffer,
                    converted * channels,
                    vorbisBuffer,
                    channels);
        delete[] resampledBuffer;

        vorbis_analysis_wrote( &vorbisDspState, converted);

    } else {

        vorbisBuffer = vorbis_analysis_buffer( &vorbisDspState, nSamples);
        Util::conv( shortBuffer, totalSamples, vorbisBuffer, channels);
        vorbis_analysis_wrote( &vorbisDspState, nSamples);
    }

    delete[] shortBuffer;
    vorbisBlocksOut();

    return processed;
}


/*------------------------------------------------------------------------------
 *  Flush the data from the encoder
 *----------------------------------------------------------------------------*/
void
VorbisLibEncoder :: flush ( void )
                                                            throw ( Exception )
{
    if ( !isOpen() ) {
        return;
    }

    vorbis_analysis_wrote( &vorbisDspState, 0);
    vorbisBlocksOut();
    sink->flush();
}


/*------------------------------------------------------------------------------
 *  Send pending Vorbis blocks to the underlying stream
 *----------------------------------------------------------------------------*/
void
VorbisLibEncoder :: vorbisBlocksOut ( void )                throw ( Exception )
{
    while ( 1 == vorbis_analysis_blockout( &vorbisDspState, &vorbisBlock) ) {
        ogg_packet      oggPacket;
        ogg_page        oggPage;

        vorbis_analysis( &vorbisBlock, &oggPacket);
        vorbis_bitrate_addblock( &vorbisBlock);

        while ( vorbis_bitrate_flushpacket( &vorbisDspState, &oggPacket) ) {

            ogg_stream_packetin( &oggStreamState, &oggPacket);

            while ( ogg_stream_pageout( &oggStreamState, &oggPage) ) {
                int    written;
                
                written  = sink->write( oggPage.header, oggPage.header_len);
                written += sink->write( oggPage.body, oggPage.body_len);

                if ( written < oggPage.header_len + oggPage.body_len ) {
                    // just let go data that could not be written
                    reportEvent( 2,
                           "couldn't write full vorbis data to underlying sink",
                           oggPage.header_len + oggPage.body_len - written);
                }
            }
        }
    }
}


/*------------------------------------------------------------------------------
 *  Close the encoding session
 *----------------------------------------------------------------------------*/
void
VorbisLibEncoder :: close ( void )                    throw ( Exception )
{
    if ( isOpen() ) {
        flush();

        ogg_stream_clear( &oggStreamState);
        vorbis_block_clear( &vorbisBlock);
        vorbis_dsp_clear( &vorbisDspState);
        vorbis_comment_clear( &vorbisComment);
        vorbis_info_clear( &vorbisInfo);

        encoderOpen = false;

        sink->close();
    }
}


#endif // HAVE_VORBIS_LIB


/*------------------------------------------------------------------------------
 
  $Source$

  $Log$
  Revision 1.16  2002/10/19 13:31:46  darkeye
  some cleanup with the open() / close() functions

  Revision 1.15  2002/10/19 12:22:10  darkeye
  return 0 immediately for write() if supplied length is 0

  Revision 1.14  2002/08/22 21:52:08  darkeye
  bug fix: maximum bitrate setting fixed for Ogg Vorbis streams

  Revision 1.13  2002/08/20 19:35:37  darkeye
  added possibility to specify maximum bitrate for Ogg Vorbis streams

  Revision 1.12  2002/08/03 10:30:46  darkeye
  resampling bugs fixed for vorbis streams

  Revision 1.11  2002/07/20 16:37:06  darkeye
  added fault tolerance in case a server connection is dropped

  Revision 1.10  2002/07/20 10:59:00  darkeye
  added support for Ogg Vorbis 1.0, removed support for rc2

  Revision 1.9  2002/05/28 12:35:41  darkeye
  code cleanup: compiles under gcc-c++ 3.1, using -pedantic option

  Revision 1.8  2002/04/13 11:26:00  darkeye
  added cbr, abr and vbr setting feature with encoding quality

  Revision 1.7  2002/03/28 16:47:38  darkeye
  moved functions conv8() and conv16() to class Util (as conv())
  added resampling functionality
  added support for variable bitrates

  Revision 1.6  2002/02/20 10:35:35  darkeye
  updated to work with Ogg Vorbis libs rc3 and current IceCast2 cvs

  Revision 1.5  2001/10/21 13:08:18  darkeye
  fixed incorrect vorbis bitrate setting

  Revision 1.4  2001/10/19 12:39:42  darkeye
  created configure options to compile with or without lame / Ogg Vorbis

  Revision 1.3  2001/09/18 14:57:19  darkeye
  finalized Solaris port

  Revision 1.2  2001/09/15 11:36:22  darkeye
  added function vorbisBlocksOut(), finalized vorbis support

  Revision 1.1  2001/09/14 19:31:06  darkeye
  added IceCast2 / vorbis support


  
------------------------------------------------------------------------------*/

