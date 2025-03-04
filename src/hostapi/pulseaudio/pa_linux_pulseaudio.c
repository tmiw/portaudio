
/*
 * PulseAudio host to play natively in Linux based systems without
 * ALSA emulation
 *
 * Copyright (c) 2014-2020 Tuukka Pasanen <tuukka.pasanen@ilmi.fi>
 * Copyright (c) 2016 Sqweek
 * Copyright (c) 2020 Daniel Schürmann
 *
 * Based on the Open Source API proposed by Ross Bencina
 * Copyright (c) 1999-2002 Ross Bencina, Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

/** @file
 @ingroup common_src

 @brief PulseAudio implementation of support for a host API.

 This host API implements PulseAudio support for portaudio
 it has callbackmode and normal write mode support
*/


#include <string.h>     /* strlen() */

#include "pa_linux_pulseaudio_cb_internal.h"
#include "pa_linux_pulseaudio_block_internal.h"

/* PulseAudio headers */
#include <stdio.h>
#include <string.h>
#include <pulse/pulseaudio.h>

/* This is used to identify process name for PulseAudio. */
extern char *__progname;

/* PulseAudio specific functions */
int PaPulseAudio_CheckConnection( PaPulseAudio_HostApiRepresentation * ptr )
{
    pa_context_state_t state;


    /* Sanity check if ptr if NULL don't go anywhere or
     * it will SIGSEGV
     */
    if( !ptr )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0,
                                          "PaPulseAudio_CheckConnection: Host API is NULL! Can't do anything about it");
        return -1;
    }

    if( !ptr->context || !ptr->mainloop )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0,
                                          "PaPulseAudio_CheckConnection: PulseAudio context or mainloop are NULL");
        return -1;
    }

    state = pa_context_get_state(ptr->context);

    if( !PA_CONTEXT_IS_GOOD(state) )
    {
        switch( state )
        {
            /* These can be found from
             * https://freedesktop.org/software/pulseaudio/doxygen/def_8h.html
             */

        	case PA_CONTEXT_UNCONNECTED:
               PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PaPulseAudio_CheckConnection: The context hasn't been connected yet (PA_CONTEXT_UNCONNECTED)");
            break;

        	case PA_CONTEXT_FAILED:
               PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0, "PaPulseAudio_CheckConnection: The connection failed or was disconnected. (PA_CONTEXT_FAILED)");
            break;

        }

        return -1;
    }

    return 0;
}

PaPulseAudio_HostApiRepresentation *PaPulseAudio_New( void )
{
    PaPulseAudio_HostApiRepresentation *ptr;
    int fd[2] = { -1, -1 };
    char l_strDeviceName[PAPULSEAUDIO_MAX_DEVICENAME];

    ptr = (PaPulseAudio_HostApiRepresentation *)
          PaUtil_AllocateMemory(sizeof(PaPulseAudio_HostApiRepresentation));

    /* ptr is NULL if runs out of memory or pointer to allocated memory */
    if( !ptr )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0,
                                          "PaPulseAudio_HostApiRepresentation: Can't allocate memory required for using PulseAudio");
        return NULL;
    }

    /* Make sure we have NULL all struct first */
    memset(ptr, 0x00, sizeof(PaPulseAudio_HostApiRepresentation));

    ptr->mainloop = pa_threaded_mainloop_new();

    if( !ptr->mainloop )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0,
                                          "PaPulseAudio_HostApiRepresentation: Can't allocate PulseAudio mainloop");
        goto fail;
    }

    /* Use program name as PulseAudio device name */
    snprintf( l_strDeviceName, PAPULSEAUDIO_MAX_DEVICENAME, "%s", __progname );

    ptr->context =
        pa_context_new( pa_threaded_mainloop_get_api(ptr->mainloop), l_strDeviceName );

    if( !ptr->context )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_HostApiRepresentation: Can't instantiate PulseAudio context" );
        goto fail;
    }

    pa_context_set_state_callback( ptr->context, PaPulseAudio_CheckContextStateCb,
                                   ptr );


    if( pa_threaded_mainloop_start( ptr->mainloop ) < 0 )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_HostApiRepresentation: PulseAudio can't start mainloop" );
        goto fail;
    }

    ptr->deviceCount = 0;

    return ptr;

  fail:
    PaPulseAudio_Free( ptr );
    return NULL;
}

void PaPulseAudio_Free( PaPulseAudio_HostApiRepresentation * ptr )
{

    /* Sanity check if ptr if NULL don't go anywhere or
     * it will SIGSEGV
     */
    if( !ptr )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR(0,
                                          "PaPulseAudio_Free: Host API is NULL! Can't do anything about it");
        return;
    }

    if( ptr->mainloop )
    {
        pa_threaded_mainloop_stop( ptr->mainloop );
    }

    if( ptr->context )
    {
        pa_context_disconnect( ptr->context );
        pa_context_unref( ptr->context );
    }

    if( ptr->mainloop )
    {
        pa_threaded_mainloop_free( ptr->mainloop );
    }

    PaUtil_FreeMemory( ptr );
}


void PaPulseAudio_CheckContextStateCb( pa_context * c,
                                      void *userdata )
{
    PaPulseAudio_HostApiRepresentation *ptr =
      (PaPulseAudio_HostApiRepresentation *) userdata;
    /* If this is null we have big problems and we probably are out of memory */
    if( !c )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_CheckContextStateCb: Invalid context " );
        pa_threaded_mainloop_signal(ptr->mainloop, 0);
        return;
    }

    pa_threaded_mainloop_signal( ptr->mainloop, 0 );
}

void PaPulseAudio_ServerInfoCb( pa_context *c,
                                const pa_server_info *i,
                                void *userdata )
{
  PaPulseAudio_HostApiRepresentation *l_ptrHostApi =
      (PaPulseAudio_HostApiRepresentation *) userdata;
  PaError result = paNoError;
  const char *l_strName = NULL;

  l_ptrHostApi->pulseaudioDefaultSource[0] = '\0';
  l_ptrHostApi->pulseaudioDefaultSink[0] = '\0';

  if( !c  || !i )
  {
      PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                         "PaPulseAudio_ServerInfoCb: Invalid context or can't get server info" );
      pa_threaded_mainloop_signal(l_ptrHostApi->mainloop, 0);
      return;
  }


  if( i->default_sink_name != NULL )
  {
    /* Make sure there is '\0' is at the end of string with using snprintf */
    snprintf( l_ptrHostApi->pulseaudioDefaultSink,
              PAPULSEAUDIO_MAX_DEVICENAME,
              "%s",
              i->default_sink_name );
  }

  if( i->default_source_name != NULL )
  {
    /* Make sure there is '\0' is at the end of string with using snprintf */
    snprintf( l_ptrHostApi->pulseaudioDefaultSource,
              PAPULSEAUDIO_MAX_DEVICENAME,
              "%s",
              i->default_source_name );
  }

  pa_threaded_mainloop_signal( l_ptrHostApi->mainloop,
                               0 );
}


int _PaPulseAudio_AddAudioDevice( PaPulseAudio_HostApiRepresentation *hostapi,
                                  const char *PaPulseAudio_SinkSourceName,
                                  const char *PaPulseAudio_SinkSourceNameDesc,
                                  int inputChannels,
                                  int outputChannels,
                                  double defaultLowInputLatency,
                                  double defaultHighInputLatency,
                                  double defaultLowOutputLatency,
                                  double defaultHighOutputLatency,
                                  const long defaultSampleRate )
{
    /* These should be at least 1 */
    int l_iRealNameSize = strnlen(PaPulseAudio_SinkSourceNameDesc, PAPULSEAUDIO_MAX_DEVICENAME - 1) + 1;
    int l_iDeviceNameSize = strnlen(PaPulseAudio_SinkSourceName, PAPULSEAUDIO_MAX_DEVICENAME - 1) + 1;
    char *l_strLocalName = NULL;

    hostapi->deviceInfoArray[hostapi->deviceCount].structVersion = 2;
    hostapi->deviceInfoArray[hostapi->deviceCount].hostApi = hostapi->hostApiIndex;
    hostapi->pulseaudioDeviceNames[hostapi->deviceCount] =
        PaUtil_GroupAllocateMemory( hostapi->allocations,
                                    l_iRealNameSize );
    l_strLocalName = PaUtil_GroupAllocateMemory( hostapi->allocations,
                                                 l_iDeviceNameSize );
    if( !hostapi->pulseaudioDeviceNames[hostapi->deviceCount] &&
        !l_strLocalName )
    {
       PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                          "_PaPulseAudio_AddAudioDevice: Can't alloc memory" );
       return paInsufficientMemory;
    }

    snprintf( hostapi->pulseaudioDeviceNames[hostapi->deviceCount],
              l_iRealNameSize,
              "%s",
              PaPulseAudio_SinkSourceNameDesc );
    snprintf( l_strLocalName,
              l_iDeviceNameSize,
              "%s",
              PaPulseAudio_SinkSourceName );

    if( !strncmp( PaPulseAudio_SinkSourceNameDesc,
                  hostapi->pulseaudioDefaultSource,
                  PAPULSEAUDIO_MAX_DEVICENAME ) )
    {
        hostapi->inheritedHostApiRep.info.defaultInputDevice =
            hostapi->deviceCount;
    }

    if( !strncmp( PaPulseAudio_SinkSourceNameDesc,
                  hostapi->pulseaudioDefaultSink,
                  PAPULSEAUDIO_MAX_DEVICENAME ) )
    {
        hostapi->inheritedHostApiRep.info.defaultOutputDevice =
            hostapi->deviceCount;
    }

    if( hostapi->deviceCount >= PAPULSEAUDIO_MAX_DEVICECOUNT )
    {
        return paDeviceUnavailable;
    }

    hostapi->deviceInfoArray[hostapi->deviceCount].name = l_strLocalName;

    hostapi->deviceInfoArray[hostapi->deviceCount].maxInputChannels = inputChannels;
    hostapi->deviceInfoArray[hostapi->deviceCount].maxOutputChannels = outputChannels;
    hostapi->deviceInfoArray[hostapi->deviceCount].defaultLowInputLatency = defaultLowInputLatency;
    hostapi->deviceInfoArray[hostapi->deviceCount].defaultLowOutputLatency = defaultLowOutputLatency;
    hostapi->deviceInfoArray[hostapi->deviceCount].defaultHighInputLatency = defaultHighOutputLatency;
    hostapi->deviceInfoArray[hostapi->deviceCount].defaultHighOutputLatency = defaultHighOutputLatency;
    hostapi->deviceInfoArray[hostapi->deviceCount].defaultSampleRate = defaultSampleRate;
    hostapi->deviceCount++;

    return paNoError;
}

void PaPulseAudio_SinkListCb( pa_context * c,
                              const pa_sink_info * l,
                              int eol,
                              void *userdata )
{
    PaPulseAudio_HostApiRepresentation *l_ptrHostApi =
        (PaPulseAudio_HostApiRepresentation *) userdata;
    PaError result = paNoError;
    const char *l_strName = NULL;


    /* If this is null we have big problems and we probably are out of memory */
    if( !c || !l )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_SinkListCb: Invalid context or sink info" );
        goto error;
    }

    /* If eol is set to a positive number, you're at the end of the list */
    if( eol > 0 )
    {
        goto error;
    }

    l_strName = l->name;

    if( l->description != NULL )
    {
        l_strName = l->description;
    }

    if( _PaPulseAudio_AddAudioDevice( l_ptrHostApi,
                                      l_strName,
                                      l->name,
                                      0,
                                      l->sample_spec.channels,
                                      0,
                                      0,
                                      l->latency / (double)PA_USEC_PER_SEC,
                                      l->configured_latency / (double)PA_USEC_PER_SEC,
                                      l->sample_spec.rate ) != paNoError )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_SinkListCb: Can't add device. Maximum amount reached!" );
    }

  error:
    pa_threaded_mainloop_signal( l_ptrHostApi->mainloop,
                                 0 );
}

void PaPulseAudio_SourceListCb( pa_context * c,
                                const pa_source_info * l,
                                int eol,
                                void *userdata )
{
    PaPulseAudio_HostApiRepresentation *l_ptrHostApi =
        (PaPulseAudio_HostApiRepresentation *) userdata;
    PaError result = paNoError;
    const char *l_strName = NULL;


    /* If this is null we have big problems and we probably are out of memory */
    if( !c )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_SourceListCb: Invalid context" );
        goto error;
    }

    /* If eol is set to a positive number, you're at the end of the list */
    if( eol > 0 )
    {
        goto error;
    }

    l_strName = l->name;

    if( l->description != NULL )
    {
        l_strName = l->description;
    }

    if( _PaPulseAudio_AddAudioDevice( l_ptrHostApi,
                                      l_strName,
                                      l->name,
                                      l->sample_spec.channels,
                                      0,
                                      l->latency / (double)PA_USEC_PER_SEC,
                                      l->configured_latency / (double)PA_USEC_PER_SEC,
                                      0,
                                      0,
                                      l->sample_spec.rate ) != paNoError )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_SourceListCb: Can't add device. Maximum amount reached!" );
    }

  error:
    pa_threaded_mainloop_signal( l_ptrHostApi->mainloop,
                                 0 );
}

/* This routine is called whenever the stream state changes */
void PaPulseAudio_StreamStateCb( pa_stream * s,
                                 void *userdata )
{
    const pa_buffer_attr *l_SBufferAttr = NULL;
    /* If you need debug pring enable these
     * char cmt[PA_CHANNEL_MAP_SNPRINT_MAX], sst[PA_SAMPLE_SPEC_SNPRINT_MAX];
     */


    /* If this is null we have big problems and we probably are out of memory */
    if( !s )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_StreamStateCb: Invalid stream" );
        return;
    }

    switch( pa_stream_get_state(s) )
    {
        case PA_STREAM_TERMINATED:
            break;

        case PA_STREAM_CREATING:
            break;

        case PA_STREAM_READY:
            if (!(l_SBufferAttr = pa_stream_get_buffer_attr(s)))
            {
                PA_DEBUG( ("Portaudio %s: Can get buffer attr: '%s'\n",
                           __FUNCTION__,
                           pa_strerror(pa_context_errno(pa_stream_get_context(s) ) ) ) );
                PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_StreamStateCb: Can't get Stream pa_buffer_attr" );
            }
            else
            {
                PA_DEBUG( ("%s: Buffer metrics: maxlength=%u, tlength=%u, prebuf=%u, minreq=%u, fragsize=%u\n",
                           __FUNCTION__,
                           l_SBufferAttr->maxlength, l_SBufferAttr->tlength, l_SBufferAttr->prebuf,
                           l_SBufferAttr->minreq, l_SBufferAttr->maxlength, l_SBufferAttr->fragsize) );
            }
            break;

        case PA_STREAM_FAILED:
        default:
            PA_DEBUG( ("Portaudio %s: FAILED '%s'\n",
                      __FUNCTION__,
                      pa_strerror( pa_context_errno( pa_stream_get_context( s ) ) ) ) );

            break;
    }
}

void PaPulseAudio_StreamUnderflowCb( pa_stream * s,
                                     void *userdata )
{
    PaPulseAudio_Stream *stream = (PaPulseAudio_Stream *) userdata;


    /* If this is null we have big problems and we probably are out of memory */
    if( !s )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PaPulseAudio_StreamUnderflowCb: Invalid stream" );
        return;
    }

    stream->outputUnderflows++;

    PA_DEBUG( ("Portaudio %s: PulseAudio playback stream has underflowed %d times\n",
               __FUNCTION__,
               stream->underflow ) );

    PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                       "PaPulseAudio_StreamUnderflowCb: Pulseaudio stream underflow");

    pa_threaded_mainloop_signal( stream->mainloop,
                                 0 );
}


PaError PaPulseAudio_Initialize( PaUtilHostApiRepresentation ** hostApi,
                                 PaHostApiIndex hostApiIndex )
{
    PaError result = paNoError;
    int i;
    int deviceCount;
    int l_iRtn = 0;
    PaPulseAudio_HostApiRepresentation *l_ptrPulseAudioHostApi = NULL;
    PaDeviceInfo *deviceInfoArray = NULL;

    pa_operation *l_ptrOperation = NULL;

    l_ptrPulseAudioHostApi = PaPulseAudio_New();

    if( !l_ptrPulseAudioHostApi )
    {
        result = paInsufficientMemory;
        goto error;
    }

    l_ptrPulseAudioHostApi->allocations = PaUtil_CreateAllocationGroup();

    if( !l_ptrPulseAudioHostApi->allocations )
    {
        result = paInsufficientMemory;
        goto error;
    }

    l_ptrPulseAudioHostApi->hostApiIndex = hostApiIndex;
    *hostApi = &l_ptrPulseAudioHostApi->inheritedHostApiRep;
    (*hostApi)->info.structVersion = 1;
    (*hostApi)->info.type = paPulseAudio;
    (*hostApi)->info.name = "PulseAudio";

    (*hostApi)->info.defaultInputDevice = paNoDevice;
    (*hostApi)->info.defaultOutputDevice = paNoDevice;

    /* Connect to server */
    pa_threaded_mainloop_lock( l_ptrPulseAudioHostApi->mainloop );
    l_iRtn = pa_context_connect( l_ptrPulseAudioHostApi->context,
                                 NULL,
                                 0,
                                 NULL );

    if( l_iRtn < 0 )
    {
        PA_PULSEAUDIO_SET_LAST_HOST_ERROR( 0,
                                           "PulseAudio_Initialize: Can't connect to server");
        result = paNotInitialized;
        pa_threaded_mainloop_unlock( l_ptrPulseAudioHostApi->mainloop );
        goto error;
    }

    l_iRtn = 0;

    /* We should wait that PulseAudio server let us in or fails us */
    while( !l_iRtn )
    {
        pa_threaded_mainloop_wait( l_ptrPulseAudioHostApi->mainloop );

        switch( pa_context_get_state( l_ptrPulseAudioHostApi->context ) )
        {
            case PA_CONTEXT_READY:
                l_iRtn = 1;
                break;
            case PA_CONTEXT_TERMINATED:
            case PA_CONTEXT_FAILED:
                goto error;
                break;
            case PA_CONTEXT_UNCONNECTED:
            case PA_CONTEXT_CONNECTING:
            case PA_CONTEXT_AUTHORIZING:
            case PA_CONTEXT_SETTING_NAME:
                break;
        }
    }

    memset( l_ptrPulseAudioHostApi->deviceInfoArray,
            0x00,
            sizeof(PaDeviceInfo) * PAPULSEAUDIO_MAX_DEVICECOUNT );
    for (i = 0; i < PAPULSEAUDIO_MAX_DEVICECOUNT; i++)
    {
        l_ptrPulseAudioHostApi->pulseaudioDeviceNames[i] = NULL;
    }

    /* Get info about server. This returns Default sink and soure name. */
    l_ptrOperation =
    pa_context_get_server_info( l_ptrPulseAudioHostApi->context,
                                PaPulseAudio_ServerInfoCb,
                                l_ptrPulseAudioHostApi );

    while( pa_operation_get_state( l_ptrOperation ) == PA_OPERATION_RUNNING )
    {
        pa_threaded_mainloop_wait( l_ptrPulseAudioHostApi->mainloop );
    }

    pa_operation_unref( l_ptrOperation );

    /* List PulseAudio sinks. If found callback: PaPulseAudio_SinkListCb */
    l_ptrOperation =
        pa_context_get_sink_info_list( l_ptrPulseAudioHostApi->context,
                                       PaPulseAudio_SinkListCb,
                                       l_ptrPulseAudioHostApi );

    while( pa_operation_get_state( l_ptrOperation ) == PA_OPERATION_RUNNING )
    {
        pa_threaded_mainloop_wait( l_ptrPulseAudioHostApi->mainloop );
    }

    pa_operation_unref( l_ptrOperation );

    /* List PulseAudio sources. If found callback: PaPulseAudio_SourceListCb */
    l_ptrOperation =
        pa_context_get_source_info_list( l_ptrPulseAudioHostApi->context,
                                         PaPulseAudio_SourceListCb,
                                         l_ptrPulseAudioHostApi );

    while( pa_operation_get_state( l_ptrOperation ) == PA_OPERATION_RUNNING )
    {
        pa_threaded_mainloop_wait( l_ptrPulseAudioHostApi->mainloop );
    }

    pa_operation_unref(l_ptrOperation);

    (*hostApi)->info.deviceCount = l_ptrPulseAudioHostApi->deviceCount;

    if( l_ptrPulseAudioHostApi->deviceCount > 0 )
    {
        /* If you have over 1024 Audio devices.. shame on you! */

        (*hostApi)->deviceInfos =
            (PaDeviceInfo **)
            PaUtil_GroupAllocateMemory( l_ptrPulseAudioHostApi->allocations,
                                        sizeof(PaDeviceInfo *) *
                                        l_ptrPulseAudioHostApi->deviceCount );

        if( !(*hostApi)->deviceInfos )
        {
            result = paInsufficientMemory;
            goto error;
        }

        for ( i = 0; i < l_ptrPulseAudioHostApi->deviceCount; i++ )
        {
            (*hostApi)->deviceInfos[i] =
                &l_ptrPulseAudioHostApi->deviceInfoArray[i];
        }
    }

    (*hostApi)->Terminate = Terminate;
    (*hostApi)->OpenStream = OpenStream;
    (*hostApi)->IsFormatSupported = IsFormatSupported;

    PaUtil_InitializeStreamInterface( &l_ptrPulseAudioHostApi->callbackStreamInterface,
                                      PaPulseAudio_CloseStreamCb,
                                      PaPulseAudio_StartStreamCb,
                                      PaPulseAudio_StopStreamCb,
                                      PaPulseAudio_AbortStreamCb,
                                      IsStreamStopped,
                                      IsStreamActive,
                                      GetStreamTime,
                                      GetStreamCpuLoad,
                                      PaUtil_DummyRead,
                                      PaUtil_DummyWrite,
                                      PaUtil_DummyGetReadAvailable,
                                      PaUtil_DummyGetWriteAvailable );

    PaUtil_InitializeStreamInterface( &l_ptrPulseAudioHostApi->blockingStreamInterface,
                                      PaPulseAudio_CloseStreamCb,
                                      PaPulseAudio_StartStreamCb,
                                      PaPulseAudio_StopStreamCb,
                                      PaPulseAudio_AbortStreamCb,
                                      IsStreamStopped,
                                      IsStreamActive,
                                      GetStreamTime,
                                      PaUtil_DummyGetCpuLoad,
                                      PaPulseAudio_ReadStreamBlock,
                                      PaPulseAudio_WriteStreamBlock,
                                      PaPulseAudio_GetStreamReadAvailableBlock,
                                      PaPulseAudio_GetStreamWriteAvailableBlock );

    pa_threaded_mainloop_unlock( l_ptrPulseAudioHostApi->mainloop );
    return result;

  error:

    if( l_ptrPulseAudioHostApi )
    {
        if( l_ptrPulseAudioHostApi->allocations )
        {
            PaUtil_FreeAllAllocations( l_ptrPulseAudioHostApi->allocations );
            PaUtil_DestroyAllocationGroup( l_ptrPulseAudioHostApi->allocations );
        }

        PaPulseAudio_Free( l_ptrPulseAudioHostApi );
        l_ptrPulseAudioHostApi = NULL;
    }

    pa_threaded_mainloop_unlock( l_ptrPulseAudioHostApi->mainloop );
    return result;
}


void Terminate( struct PaUtilHostApiRepresentation *hostApi )
{
    PaPulseAudio_HostApiRepresentation *l_ptrPulseAudioHostApi =
        (PaPulseAudio_HostApiRepresentation *) hostApi;

    if( l_ptrPulseAudioHostApi->allocations )
    {
        PaUtil_FreeAllAllocations( l_ptrPulseAudioHostApi->allocations );
        PaUtil_DestroyAllocationGroup( l_ptrPulseAudioHostApi->allocations );
    }

    pa_threaded_mainloop_lock( l_ptrPulseAudioHostApi->mainloop );
    pa_context_disconnect( l_ptrPulseAudioHostApi->context );
    pa_threaded_mainloop_unlock( l_ptrPulseAudioHostApi->mainloop );

    PaPulseAudio_Free( l_ptrPulseAudioHostApi );
}


PaError IsFormatSupported( struct PaUtilHostApiRepresentation *hostApi,
                           const PaStreamParameters * inputParameters,
                           const PaStreamParameters * outputParameters,
                           double sampleRate )
{
    int inputChannelCount,
     outputChannelCount;
    PaSampleFormat inputSampleFormat,
     outputSampleFormat;

    if( inputParameters )
    {
        inputChannelCount = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;

        /* all standard sample formats are supported by the buffer adapter,
         * this implementation doesn't support any custom sample formats */
        if( inputSampleFormat & paCustomFormat )
        {
            return paSampleFormatNotSupported;
        }

        /* unless alternate device specification is supported, reject the use of
         * paUseHostApiSpecificDeviceSpecification */

        if( inputParameters->device == paUseHostApiSpecificDeviceSpecification )
        {
            return paInvalidDevice;
        }

        /* check that input device can support inputChannelCount */
        if( inputChannelCount >
            hostApi->deviceInfos[inputParameters->device]->maxInputChannels )
        {
            return paInvalidChannelCount;
        }

        /* validate inputStreamInfo */
        if( inputParameters->hostApiSpecificStreamInfo )
        {
            return paIncompatibleHostApiSpecificStreamInfo;     /* this implementation doesn't use custom stream info */
        }

    }

    else
    {
        inputChannelCount = 0;
    }

    if( outputParameters)
    {
        outputChannelCount = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;

        /* all standard sample formats are supported by the buffer adapter,
         * this implementation doesn't support any custom sample formats
         */
        if( outputSampleFormat & paCustomFormat )
        {
            return paSampleFormatNotSupported;
        }

        /* unless alternate device specification is supported, reject the use of
         * paUseHostApiSpecificDeviceSpecification
         */
        if( outputParameters->device == paUseHostApiSpecificDeviceSpecification )
        {
            return paInvalidDevice;
        }

        /* check that output device can support outputChannelCount */
        if( outputChannelCount >
            hostApi->deviceInfos[outputParameters->device]->maxOutputChannels )
        {
            return paInvalidChannelCount;
        }

        /* validate outputStreamInfo */
        if( outputParameters->hostApiSpecificStreamInfo )
        {
            /* this implementation doesn't use custom stream info */
            return paIncompatibleHostApiSpecificStreamInfo;
        }

    }

    else
    {
        outputChannelCount = 0;
    }

    return paFormatIsSupported;
}

PaError PaPulseAudio_ConvertPortaudioFormatToPaPulseAudio_( PaSampleFormat portaudiosf,
                                                            pa_sample_spec * pulseaudiosf )
{
    switch( portaudiosf )
    {
        case paFloat32:
            pulseaudiosf->format = PA_SAMPLE_FLOAT32LE;
            break;

        case paInt32:
            pulseaudiosf->format = PA_SAMPLE_S32LE;
            break;

        case paInt24:
            pulseaudiosf->format = PA_SAMPLE_S24LE;
            break;

        case paInt16:
            pulseaudiosf->format = PA_SAMPLE_S16LE;
            break;

        case paInt8:
            pulseaudiosf->format = PA_SAMPLE_U8;
            break;

        case paUInt8:
            pulseaudiosf->format = PA_SAMPLE_U8;
            break;

        case paCustomFormat:
        case paNonInterleaved:
            PA_DEBUG(("PaPulseAudio %s: THIS IS NOT SUPPORTED BY PULSEAUDIO!\n",
                      __FUNCTION__));
            return paSampleFormatNotSupported;
            break;
    }

    return paNoError;
}


/* Allocate buffer. */
PaError PaPulseAudio_BlockingInitRingBuffer( PaPulseAudio_Stream * stream,
                                             PaUtilRingBuffer * rbuf,
                                             int size )
{
    long l_lNumBytes = 4096 * size;
    char *l_ptrBuffer = (char *) malloc(l_lNumBytes);

    if( l_ptrBuffer == NULL )
    {
        return paInsufficientMemory;
    }

    memset( l_ptrBuffer,
            0x00,
            l_lNumBytes );

    return (PaError) PaUtil_InitializeRingBuffer( rbuf,
                                                  1,
                                                  l_lNumBytes,
                                                  l_ptrBuffer );
}

/* see pa_hostapi.h for a list of validity guarantees made about OpenStream parameters */

PaError OpenStream( struct PaUtilHostApiRepresentation *hostApi,
                    PaStream ** s,
                    const PaStreamParameters * inputParameters,
                    const PaStreamParameters * outputParameters,
                    double sampleRate,
                    unsigned long framesPerBuffer,
                    PaStreamFlags streamFlags,
                    PaStreamCallback * streamCallback,
                    void *userData )
{
    PaError result = paNoError;
    PaPulseAudio_HostApiRepresentation *l_ptrPulseAudioHostApi =
        (PaPulseAudio_HostApiRepresentation *) hostApi;
    PaPulseAudio_Stream *stream = NULL;
    unsigned long framesPerHostBuffer = framesPerBuffer;        /* these may not be equivalent for all implementations */
    int inputChannelCount,
     outputChannelCount;
    PaSampleFormat inputSampleFormat,
     outputSampleFormat;
    PaSampleFormat hostInputSampleFormat,
     hostOutputSampleFormat;

    /* validate platform specific flags */
    if( (streamFlags & paPlatformSpecificFlags) != 0 )
    {
        return paInvalidFlag;   /* unexpected platform specific flag */
    }


    pa_threaded_mainloop_lock(l_ptrPulseAudioHostApi->mainloop);
    stream =
        (PaPulseAudio_Stream *) PaUtil_AllocateMemory( sizeof( PaPulseAudio_Stream ) );

    if( !stream )
    {
        result = paInsufficientMemory;
        goto error;
    }

    /* Allocate memory for source and sink names. */
    const char defaultSourceStreamName[] = "Portaudio source";
    const char defaultSinkStreamName[] = "Portaudio sink";

    stream->sourceStreamName = (char*)PaUtil_AllocateMemory(sizeof(defaultSourceStreamName));
    stream->sinkStreamName = (char*)PaUtil_AllocateMemory(sizeof(defaultSinkStreamName));
    if ( !stream->sourceStreamName || !stream->sinkStreamName )
    {
        result = paInsufficientMemory;
        goto error;
    }

    /* Copy initial stream names to memory. */
    memcpy( stream->sourceStreamName, defaultSourceStreamName, sizeof(defaultSourceStreamName) );
    memcpy( stream->sinkStreamName, defaultSinkStreamName, sizeof(defaultSinkStreamName) );
 
    stream->isActive = 0;
    stream->isStopped = 1;

    stream->inStream = NULL;
    stream->outStream = NULL;
    stream->outBuffer = NULL;
    stream->inBuffer = NULL;
    memset( &stream->outputRing,
            0x00,
            sizeof(PaUtilRingBuffer) );
    memset( &stream->inputRing,
            0x00,
            sizeof(PaUtilRingBuffer) );


    if( inputParameters )
    {
        inputChannelCount = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;

        /* unless alternate device specification is supported, reject the use of
         * paUseHostApiSpecificDeviceSpecification
         */
        if( inputParameters->device == paUseHostApiSpecificDeviceSpecification )
        {
            result = paInvalidDevice;
            goto error;
        }

        /* check that input device can support inputChannelCount */
        if( inputChannelCount >
            hostApi->deviceInfos[inputParameters->device]->maxInputChannels )
        {
            result = paInvalidChannelCount;
            goto error;
        }

        /* validate inputStreamInfo */
        if( inputParameters->hostApiSpecificStreamInfo )
        {
            /* this implementation doesn't use custom stream info */
            result = paIncompatibleHostApiSpecificStreamInfo;
            goto error;
        }

        hostInputSampleFormat =
            PaUtil_SelectClosestAvailableFormat( inputSampleFormat,
                                                 inputSampleFormat );

        stream->inputFrameSize = Pa_GetSampleSize( inputSampleFormat ) * inputChannelCount;

        result = PaPulseAudio_ConvertPortaudioFormatToPaPulseAudio_(
            hostInputSampleFormat,
            &stream->inSampleSpec );

        if( result != paNoError )
        {
            goto error;
        }

        stream->inSampleSpec.rate = sampleRate;
        stream->inSampleSpec.channels = inputChannelCount;
        stream->latency = inputParameters->suggestedLatency;

        if( !pa_sample_spec_valid(&stream->inSampleSpec) )
        {
            PA_DEBUG( ("Portaudio %s: Invalid input audio spec!\n",
                      __FUNCTION__) );
            result = paUnanticipatedHostError; /* should have been caught above */
            goto error;
        }

        stream->inStream =
            pa_stream_new( l_ptrPulseAudioHostApi->context,
                           stream->sourceStreamName,
                           &stream->inSampleSpec,
                           NULL );

        if( stream->inStream != NULL )
        {
            pa_stream_set_state_callback( stream->inStream,
                                          PaPulseAudio_StreamStateCb,
                                          NULL);
            pa_stream_set_started_callback( stream->inStream,
                                            PaPulseAudio_StreamStartedCb,
                                            NULL );
            pa_stream_set_read_callback( stream->inStream,
                                         PaPulseAudio_StreamRecordCb,
                                         stream);

        }

        else
        {
            PA_DEBUG( ("Portaudio %s: Can't alloc input stream!\n",
                       __FUNCTION__) );
        }

        stream->inDevice = inputParameters->device;

        if( !streamCallback )
        {
            result = PaPulseAudio_BlockingInitRingBuffer( stream,
                                                          &stream->inputRing,
                                                          stream->inputFrameSize );
            if( result != paNoError )
            {
                goto error;
            }
        }

    }

    else
    {
        inputChannelCount = 0;
        inputSampleFormat = hostInputSampleFormat = paFloat32;
    }

    if( outputParameters )
    {
        outputChannelCount = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;

        /* unless alternate device specification is supported, reject the use of
         * paUseHostApiSpecificDeviceSpecification
         */
        if( outputParameters->device == paUseHostApiSpecificDeviceSpecification )
        {
            result = paInvalidDevice;
            goto error;
        }

        /* check that output device can support inputChannelCount */
        if( outputChannelCount >
            hostApi->deviceInfos[outputParameters->device]->maxOutputChannels )
        {
            result = paInvalidChannelCount;
            goto error;
        }

        /* validate outputStreamInfo */
        if( outputParameters->hostApiSpecificStreamInfo )
        {
            result = paIncompatibleHostApiSpecificStreamInfo;   /* this implementation doesn't use custom stream info */
            goto error;
        }

        /* IMPLEMENT ME - establish which  host formats are available */
        hostOutputSampleFormat =
            PaUtil_SelectClosestAvailableFormat( outputSampleFormat
                                                 /* native formats */ ,
                                                 outputSampleFormat );

        stream->outputFrameSize =
            Pa_GetSampleSize( outputSampleFormat ) * outputChannelCount;

        result = PaPulseAudio_ConvertPortaudioFormatToPaPulseAudio_(
            hostOutputSampleFormat,
            &stream->outSampleSpec );

        if( result != paNoError )
        {
            goto error;
        }

        stream->outSampleSpec.rate = sampleRate;
        stream->outSampleSpec.channels = outputChannelCount;
        stream->outputChannelCount = outputChannelCount;
        stream->latency = outputParameters->suggestedLatency;

        /* Really who has mono output anyway but whom I'm to judge? */
        if( stream->outSampleSpec.channels == 1 )
        {
          stream->outSampleSpec.channels = 2;
        }

        if( !pa_sample_spec_valid( &stream->outSampleSpec ) )
        {
            PA_DEBUG( ("Portaudio %s: Invalid audio spec for output!\n",
                      __FUNCTION__) );
            result = paUnanticipatedHostError; /* should have been caught above */
            goto error;
        }

        stream->outStream =
            pa_stream_new( l_ptrPulseAudioHostApi->context,
                           stream->sinkStreamName,
                           &stream->outSampleSpec,
                           NULL );

        if( stream->outStream != NULL )
        {
            pa_stream_set_state_callback( stream->outStream,
                                          PaPulseAudio_StreamStateCb,
                                          NULL );
            pa_stream_set_started_callback( stream->outStream,
                                            PaPulseAudio_StreamStartedCb,
                                            NULL );

            /* If we use callback then use callback in PaPulseAudio_ */
            if( streamCallback )
            {
                pa_stream_set_write_callback( stream->outStream,
                                              PaPulseAudio_StreamPlaybackCb,
                                              stream );
            }
        }

        else
        {
            PA_DEBUG( ("Portaudio %s: Can't alloc output stream!\n",
                      __FUNCTION__) );
        }

        if( !streamCallback )
        {
            result = PaPulseAudio_BlockingInitRingBuffer( stream,
                                                          &stream->outputRing,
                                                          stream->outputFrameSize );
            if( result != paNoError )
            {
                goto error;
            }
        }

        stream->outDevice = outputParameters->device;
    }

    else
    {
        outputChannelCount = 0;
        outputSampleFormat = hostOutputSampleFormat = paFloat32;
    }


    stream->hostapi = l_ptrPulseAudioHostApi;
    stream->context = l_ptrPulseAudioHostApi->context;
    stream->mainloop = l_ptrPulseAudioHostApi->mainloop;

    if( streamCallback )
    {
        PaUtil_InitializeStreamRepresentation( &stream->streamRepresentation,
                                               &l_ptrPulseAudioHostApi->callbackStreamInterface,
                                               streamCallback,
                                               userData );
    }
    else
    {
        PaUtil_InitializeStreamRepresentation( &stream->streamRepresentation,
                                               &l_ptrPulseAudioHostApi->blockingStreamInterface,
                                               streamCallback,
                                               userData );
    }

    PaUtil_InitializeCpuLoadMeasurer( &stream->cpuLoadMeasurer,
                                      sampleRate
                                    );


    /* we assume a fixed host buffer size in this example, but the buffer processor
     * can also support bounded and unknown host buffer sizes by passing
     * paUtilBoundedHostBufferSize or paUtilUnknownHostBufferSize instead of
     * paUtilFixedHostBufferSize below. */

    result = PaUtil_InitializeBufferProcessor( &stream->bufferProcessor,
                                               inputChannelCount,
                                               inputSampleFormat,
                                               hostInputSampleFormat,
                                               outputChannelCount,
                                               outputSampleFormat,
                                               hostOutputSampleFormat,
                                               sampleRate,
                                               streamFlags,
                                               framesPerBuffer,
                                               framesPerHostBuffer,
                                               paUtilUnknownHostBufferSize,
                                               streamCallback,
                                               userData );

    if( result != paNoError )
    {
        goto error;
    }

    /* inputLatency is specified in _seconds_ */
    stream->streamRepresentation.streamInfo.inputLatency =
        (PaTime) PaUtil_GetBufferProcessorInputLatencyFrames(
            &stream->bufferProcessor ) / sampleRate;

    /* outputLatency is specified in _seconds_ */
    stream->streamRepresentation.streamInfo.outputLatency =
        (PaTime) PaUtil_GetBufferProcessorOutputLatencyFrames(
            &stream->bufferProcessor ) / sampleRate;

    stream->streamRepresentation.streamInfo.sampleRate = sampleRate;

    stream->maxFramesHostPerBuffer = framesPerBuffer;
    stream->maxFramesPerBuffer = framesPerBuffer;

    *s = (PaStream *) stream;

    pa_threaded_mainloop_unlock( l_ptrPulseAudioHostApi->mainloop );
    return result;

  error:
    pa_threaded_mainloop_unlock( l_ptrPulseAudioHostApi->mainloop );

    if( stream )
    {
        PaUtil_FreeMemory( stream->sourceStreamName );
        PaUtil_FreeMemory( stream->sinkStreamName );
        PaUtil_FreeMemory(stream);
    }

    return result;
}

PaError IsStreamStopped( PaStream * s )
{
    PaPulseAudio_Stream *stream = (PaPulseAudio_Stream *) s;
    return stream->isStopped;
}


PaError IsStreamActive( PaStream * s )
{
    PaPulseAudio_Stream *stream = (PaPulseAudio_Stream *) s;
    return stream->isActive;
}


PaTime GetStreamTime( PaStream * s )
{
    PaPulseAudio_Stream *stream = (PaPulseAudio_Stream *) s;
    PaPulseAudio_HostApiRepresentation *l_ptrPulseAudioHostApi = stream->hostapi;
    pa_usec_t l_lUSec = 0;
    pa_operation *l_ptrOperation;

    pa_threaded_mainloop_lock( l_ptrPulseAudioHostApi->mainloop );

    if( pa_stream_get_time( stream->outStream, &stream->outStreamTime ) !=
        -PA_ERR_NODATA )
    {
    }
    else
    {
        stream->outStreamTime = 0;
    }

    pa_threaded_mainloop_unlock( l_ptrPulseAudioHostApi->mainloop );
    return ((float) stream->outStreamTime / (float) 1000000);
}


double GetStreamCpuLoad( PaStream * s )
{
    PaPulseAudio_Stream *stream = (PaPulseAudio_Stream *) s;

    return PaUtil_GetCpuLoad( &stream->cpuLoadMeasurer );
}

/** Extensions */
static void RenameStreamCb(pa_stream *s, int success, void *userdata)
{
    /* Currently does nothing but signal the caller. */
    PaPulseAudio_Stream *l_ptrStream = (PaPulseAudio_Stream *) userdata;
    pa_threaded_mainloop_signal( l_ptrStream->mainloop,
                                 0 );
}

PaError PaPulseAudio_RenameSource( PaStream *s, const char *streamName )
{
    PaPulseAudio_Stream *stream = (PaPulseAudio_Stream *) s;
    PaError result = paNoError;
    pa_operation *op = NULL;

    if ( stream->inStream == NULL )
    {
        return paInvalidDevice;
    }

    /* Reallocate stream name in memory. */
    pa_threaded_mainloop_lock( stream->mainloop );
    char *newStreamName = (char*)PaUtil_AllocateMemory(strnlen(streamName, PAPULSEAUDIO_MAX_DEVICENAME) + 1);
    if ( !newStreamName )
    {
        pa_threaded_mainloop_unlock( stream->mainloop );
        return paInsufficientMemory;
    }
    snprintf(newStreamName, strnlen(streamName, PAPULSEAUDIO_MAX_DEVICENAME) + 1, "%s", streamName);

    PaUtil_FreeMemory( stream->sourceStreamName );
    stream->sourceStreamName = newStreamName;

    op = pa_stream_set_name( stream->inStream, streamName, RenameStreamCb, stream );
    pa_threaded_mainloop_unlock( stream->mainloop );

    /* Wait for completion. */
    while (pa_operation_get_state( op ) == PA_OPERATION_RUNNING)
    {
        pa_threaded_mainloop_wait( stream->mainloop );
    }

    return result;
}

PaError PaPulseAudio_RenameSink( PaStream *s, const char *streamName )
{
    PaPulseAudio_Stream *stream = (PaPulseAudio_Stream *) s;
    PaError result = paNoError;
    pa_operation *op = NULL;
    
    if ( stream->outStream == NULL )
    {
        return paInvalidDevice;
    }

    /* Reallocate stream name in memory. */
    pa_threaded_mainloop_lock( stream->mainloop );
    char *newStreamName = (char*)PaUtil_AllocateMemory(strnlen(streamName, PAPULSEAUDIO_MAX_DEVICENAME) + 1);
    if ( !newStreamName )
    {
        pa_threaded_mainloop_unlock( stream->mainloop );
        return paInsufficientMemory;
    }
    snprintf(newStreamName, strnlen(streamName, PAPULSEAUDIO_MAX_DEVICENAME) + 1, "%s", streamName);

    PaUtil_FreeMemory( stream->sinkStreamName );
    stream->sinkStreamName = newStreamName;
    
    op = pa_stream_set_name( stream->outStream, streamName, RenameStreamCb, stream );
    pa_threaded_mainloop_unlock( stream->mainloop );

    /* Wait for completion. */
    while (pa_operation_get_state( op ) == PA_OPERATION_RUNNING)
    {
        pa_threaded_mainloop_wait( stream->mainloop );
    }

    return result;
}
