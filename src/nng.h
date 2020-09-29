/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Frank Quinn (http://fquinner.github.io)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MAMA_BRIDGE_NNG_NNGDEFS_H__
#define MAMA_BRIDGE_NNG_NNGDEFS_H__


/*=========================================================================
  =                             Includes                                  =
  =========================================================================*/

#include <wombat/wSemaphore.h>
#include <wombat/wtable.h>
#include <list.h>
#include <wombat/queue.h>
#include <wombat/mempool.h>
#include <mama/integration/endpointpool.h>
#include <mama/integration/msg.h>
#include <nng/nng.h>

#if defined(__cplusplus)
extern "C" {
#endif


/*=========================================================================
  =                              Macros                                   =
  =========================================================================*/

/* Maximum topic length */
#define     MAX_SUBJECT_LENGTH              256
#define     NNG_MAX_INCOMING_URIS           64
#define     NNG_MAX_OUTGOING_URIS           64

typedef enum nngTransportType_
{
    NNG_TPORT_TYPE_TCP,
    NNG_TPORT_TYPE_IPC,
    NNG_TPORT_TYPE_PGM,
    NNG_TPORT_TYPE_EPGM,
    NNG_TPORT_TYPE_UNKNOWN = 99
} nngTransportType;

typedef enum nngTransportDirection_
{
    NNG_TPORT_DIRECTION_INCOMING,
    NNG_TPORT_DIRECTION_OUTGOING
} nngTransportDirection;


/*=========================================================================
  =                Typedefs, structs, enums and globals                   =
  =========================================================================*/

typedef struct nngTransportBridge_
{
    int                     mIsValid;
    mamaTransport           mTransport;
    msgBridge               mMsg;
    nng_socket              mNngSocketSubscriber;
    nng_socket              mNngSocketPublisher;
    void*                   mNngSocketDispatcher;
    const char*             mIncomingAddress[NNG_MAX_INCOMING_URIS];
    const char*             mOutgoingAddress[NNG_MAX_OUTGOING_URIS];
    const char*             mName;
    wthread_t               mOmnngDispatchThread;
    int                     mIsDispatching;
    mama_status             mOmnngDispatchStatus;
    endpointPool_t          mSubEndpoints;
    endpointPool_t          mPubEndpoints;
    long int                mMemoryPoolSize;
    long int                mMemoryNodeSize;
} nngTransportBridge;

typedef struct nngSubscription_
{
    mamaMsgCallbacks        mMamaCallback;
    mamaSubscription        mMamaSubscription;
    mamaQueue               mMamaQueue;
    void*                   mNngQueue;
    nngTransportBridge*     mTransport;
    const char*             mSymbol;
    char*                   mSubjectKey;
    void*                   mClosure;
    int                     mIsNotMuted;
    int                     mIsValid;
    int                     mIsTportDisconnected;
    msgBridge               mMsg;
    const char*             mEndpointIdentifier;
} nngSubscription;

typedef struct nngTransportMsg_
{
    size_t                  mNodeSize;
    size_t                  mNodeCapacity;
    nngSubscription*        mSubscription;
    uint8_t*                mNodeBuffer;
} nngTransportMsg;

typedef struct nngQueueBridge {
    mamaQueue               mParent;
    wombatQueue             mQueue;
    void*                   mEnqueueClosure;
    uint8_t                 mHighWaterFired;
    size_t                  mHighWatermark;
    size_t                  mLowWatermark;
    uint8_t                 mIsDispatching;
    mamaQueueEnqueueCB      mEnqueueCallback;
    void*                   mClosure;
    wthread_mutex_t         mDispatchLock;
    void*                   mNngContext;
    void*                   mNngSocketWorker;
    void*                   mNngSocketDealer;
} nngQueueBridge;


#if defined(__cplusplus)
}
#endif

#endif /* MAMA_BRIDGE_NNG_NNGDEFS_H__ */
