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

/*=========================================================================
 =                             Includes                                  =
 =========================================================================*/

#include <string.h>
#include <stdint.h>

#include <mama/mama.h>
#include <mama/inbox.h>
#include <mama/publisher.h>
#include <mama/integration/inbox.h>
#include <mama/integration/msg.h>
#include "transport.h"
#include "nng.h"
#include "mama/integration/bridge/base.h"
#include "subscription.h"
#include "mama/integration/endpointpool.h"
#include "nngbridgefunctions.h"
#include "msg.h"

/*=========================================================================
 =                Typedefs, structs, enums and globals                   =
 =========================================================================*/

typedef struct nngPublisherBridge
{
    nngTransportBridge*     mTransport;
    const char*             mTopic;
    const char*             mSource;
    const char*             mRoot;
    const char*             mSubject;
    msgBridge               mMamaBridgeMsg;
    mamaPublisher           mParent;
    mamaPublisherCallbacks  mCallbacks;
    void*                   mCallbackClosure;
} nngPublisherBridge;

/*=========================================================================
 =                  Private implementation prototypes                    =
 =========================================================================*/

/**
 * When a nng publisher is created, it calls this function to generate a
 * standard subject key using nngBridgeMamaSubscriptionImpl_generateSubjectKey
 * with different parameters depending on if it's a market data publisher,
 * basic publisher or a data dictionary publisher.
 *
 * @param msg   The MAMA message to enqueue for sending.
 * @param url   The URL to eneueue the message for sending to.
 * @param impl  The related nng publisher bridge.
 *
 * @return mama_status indicating whether the method succeeded or failed.
 */
static mama_status
nngBridgeMamaPublisherImpl_buildSendSubject (nngPublisherBridge* impl);


/*=========================================================================
 =               Public interface implementation functions               =
 =========================================================================*/

mama_status
nngBridgeMamaPublisher_createByIndex (publisherBridge*     result,
                                      mamaTransport        tport,
                                      int                  tportIndex,
                                      const char*          topic,
                                      const char*          source,
                                      const char*          root,
                                      mamaPublisher        parent)
{
    nngPublisherBridge*    impl        = NULL;
    nngTransportBridge*    transport   = NULL;
    mama_status             status      = MAMA_STATUS_OK;

    if (NULL == result
            || NULL == tport
            || NULL == parent)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    transport = nngBridgeMamaTransportImpl_getTransportBridge (tport);
    if (NULL == transport)
    {
        mama_log (MAMA_LOG_LEVEL_SEVERE,
                  "nngBridgeMamaPublisher_createByIndex(): "
                  "Could not find transport.");
        return MAMA_STATUS_NULL_ARG;
    }

    impl = (nngPublisherBridge*) calloc (1, sizeof (nngPublisherBridge));
    if (NULL == impl)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_createByIndex(): "
                  "Could not allocate mem publisher.");
        return MAMA_STATUS_NOMEM;
    }

    /* Initialize the publisher members */
    impl->mTransport = transport;
    impl->mParent    = parent;

    /* Create an underlying bridge message with no parent to be used in sends */
    status = baseBridgeMamaMsgImpl_createMsgOnly (&impl->mMamaBridgeMsg);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_createByIndex(): "
                  "Could not create nng bridge message for publisher: %s.",
                  mamaStatus_stringForStatus (status));
        free (impl);
        return MAMA_STATUS_NOMEM;
    }

    if (NULL != topic)
    {
        impl->mTopic = topic;
    }

    if (NULL != source)
    {
        impl->mSource = source;
    }

    if (NULL != root)
    {
        impl->mRoot = root;
    }

    /* Generate a topic name based on the publisher details */
    status = nngBridgeMamaPublisherImpl_buildSendSubject (impl);

    /* Populate the publisherBridge pointer with the publisher implementation */
    *result = (publisherBridge) impl;

    return status;
}

mama_status
nngBridgeMamaPublisher_destroy (publisherBridge publisher)
{
    nngPublisherBridge*    impl    = (nngPublisherBridge*) publisher;

    /* Take a copy of the callbacks - we'll need those */
    mamaPublisherCallbacks callbacks;
    mamaPublisher          parent  = NULL;
    void*                  closure = NULL;

    if (NULL == impl)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    /* Take a copy of the callbacks - we'll need those */
    callbacks = impl->mCallbacks;
    parent    = impl->mParent;
    closure   = impl->mCallbackClosure;

    if (NULL != impl->mSubject)
    {
        free ((void*) impl->mSubject);
    }
    if (NULL != impl->mMamaBridgeMsg)
    {
        baseBridgeMamaMsg_destroy (impl->mMamaBridgeMsg, 0);
    }

    free (impl);

    if (NULL != callbacks.onDestroy)
    {
        (*callbacks.onDestroy)(parent, closure);
    }

    return MAMA_STATUS_OK;
}

mama_status
nngBridgeMamaPublisher_send (publisherBridge publisher, mamaMsg msg)
{
    mama_status           status        = MAMA_STATUS_OK;
    nngPublisherBridge*   impl          = (nngPublisherBridge*) publisher;
    char*                 url           = NULL;
    baseMsgType           type          = BASE_MSG_PUB_SUB;

    if (NULL == impl)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_send(): No publisher.");
        return MAMA_STATUS_NULL_ARG;
    }
    else if (NULL == msg)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    /* Get the bridge message type if specified already by inbox handlers */
    baseBridgeMamaMsgImpl_getMsgType (impl->mMamaBridgeMsg, &type);

    switch (type)
    {
    case BASE_MSG_INBOX_REQUEST:
        /* Use the publisher's default send subject */
        baseBridgeMamaMsg_setSendSubject (impl->mMamaBridgeMsg,
                                          impl->mSubject,
                                          impl->mSource);
        break;
    case BASE_MSG_INBOX_RESPONSE:
        /* The url should already be set for inbox responses as the replyTo */
        baseBridgeMamaMsgImpl_getDestination (impl->mMamaBridgeMsg, &url);
        break;
    default:
        /* Use the publisher's default send subject */
        baseBridgeMamaMsg_setSendSubject (impl->mMamaBridgeMsg,
                                          impl->mSubject,
                                          impl->mSource);
        break;
    }

    void* buf = NULL;
    size_t bufSize = 0;
    size_t payloadSize = 0;
    /* Pack the provided MAMA message into a proton message */
    nngBridgeMamaMsgImpl_serialize (impl->mMamaBridgeMsg, msg, &buf, &bufSize);
    baseBridgeMamaMsgImpl_getPayloadSize (impl->mMamaBridgeMsg, &payloadSize);

    nng_msg *nmsg = NULL;
    nng_msg_alloc(&nmsg, bufSize);
    void* body = nng_msg_body(nmsg);
    memcpy(body, buf, bufSize);

    int i = nng_sendmsg(impl->mTransport->mNngSocketPublisher, nmsg, 0);
    mama_log (MAMA_LOG_LEVEL_FINER,
              "nngBridgeMamaPublisher_send(): "
              "Sending %lu bytes [payload=%lu; type=%d; topic=%s; source=%s]",
              bufSize,
              payloadSize,
              type,
              i,
              impl->mSubject,
              impl->mSource);

    /* Reset the message type for the next publish */
    baseBridgeMamaMsgImpl_setMsgType (impl->mMamaBridgeMsg, BASE_MSG_PUB_SUB);

    return status;
}

/* Send reply to inbox. */
mama_status
nngBridgeMamaPublisher_sendReplyToInbox (publisherBridge   publisher,
                                           void*             request,
                                           mamaMsg           reply)
{
    nngPublisherBridge*    impl            = (nngPublisherBridge*) publisher;
    mamaMsg                requestMsg      = (mamaMsg) request;
    const char*            inboxSubject    = NULL;
    const char*            replyTo         = NULL;
    msgBridge              bridgeMsg       = NULL;
    mama_status            status          = MAMA_STATUS_OK;

    if (NULL == publisher || NULL == request || NULL == reply)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    /* Set properties for the outgoing bridge message */
    baseBridgeMamaMsgImpl_setMsgType (impl->mMamaBridgeMsg,
                                      BASE_MSG_INBOX_RESPONSE);

    /* Target is for MD subscriptions to respond to this particular topic */
    baseBridgeMamaMsgImpl_setTargetSubject (impl->mMamaBridgeMsg,
                                            impl->mSubject);

    /* Get the incoming bridge message from the mamaMsg */
    status = mamaMsgImpl_getBridgeMsg (requestMsg, &bridgeMsg);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "Could not get bridge message from cached"
                  " queue mamaMsg [%s]",
                  mamaStatus_stringForStatus (status));
        return status;
    }

    /* Get properties from the incoming bridge message */
    status = baseBridgeMamaMsgImpl_getInboxName (bridgeMsg,
                                                 (char**) &inboxSubject);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "Could not get inbox name [%s]",
                  mamaStatus_stringForStatus (status));
        return status;
    }

    status = baseBridgeMamaMsgImpl_getReplyTo (bridgeMsg, (char**) &replyTo);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "Could not get reply to [%s]",
                  mamaStatus_stringForStatus (status));
        return status;
    }

    if (NULL == inboxSubject)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "No reply address specified - cannot respond to inbox %s.",
                  inboxSubject);
        return status;
    }

    /* Set the send subject to publish onto the inbox subject */
    status = baseBridgeMamaMsg_setSendSubject (impl->mMamaBridgeMsg,
                                               inboxSubject,
                                               impl->mSource);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "Could not set send subject '%s' [%s]",
                  inboxSubject,
                  mamaStatus_stringForStatus (status));
        return status;
    }

    /* Set the destination to the replyTo URL */
    status = baseBridgeMamaMsgImpl_setDestination (impl->mMamaBridgeMsg,
                                                   replyTo);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "Could not set destination '%s' [%s]",
                  replyTo,
                  mamaStatus_stringForStatus (status));
        return status;
    }

    /* Fire out the message to the inbox */
    return nngBridgeMamaPublisher_send (publisher, reply);
}

mama_status
nngBridgeMamaPublisher_sendReplyToInboxHandle (publisherBridge     publisher,
                                               void*               inbox,
                                               mamaMsg             reply)
{
    nngPublisherBridge*       impl           = (nngPublisherBridge*) publisher;
    const char*               inboxSubject   = NULL;
    const char*               replyTo        = NULL;
    mama_status               status         = MAMA_STATUS_OK;

    if (NULL == publisher || NULL == inbox || NULL == reply)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    /* Set properties for the outgoing bridge message */
    baseBridgeMamaMsgImpl_setMsgType (impl->mMamaBridgeMsg,
                                      BASE_MSG_INBOX_RESPONSE);

    /* Target is for MD subscriptions to respond to this particular topic */
    baseBridgeMamaMsgImpl_setTargetSubject (impl->mMamaBridgeMsg,
                                           impl->mSubject);

    /* Get properties from the incoming bridge message */
    status = baseBridgeMamaMsgReplyHandleImpl_getInboxName (
                     inbox,
                     (char**) &inboxSubject);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "Could not get inbox name [%s]",
                  mamaStatus_stringForStatus (status));
        return status;
    }

    status = baseBridgeMamaMsgReplyHandleImpl_getReplyTo (
                                                inbox,
                                                (char**) &replyTo);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInbox(): "
                  "Could not get reply to [%s]",
                  mamaStatus_stringForStatus (status));
        return status;
    }


    if (NULL == inboxSubject || NULL == replyTo)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInboxHandle(): "
                  "No reply address specified - cannot respond to inbox %s.",
                  inboxSubject);
        return status;
    }

    /* Set the send subject to publish onto the inbox subject */
    status = baseBridgeMamaMsg_setSendSubject (impl->mMamaBridgeMsg,
                                               inboxSubject,
                                               impl->mSource);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInboxHandle(): "
                  "Could not set send subject '%s' [%s]",
                  inboxSubject,
                  mamaStatus_stringForStatus (status));
        return status;
    }

    /* Set the destination to the replyTo URL */
    status = baseBridgeMamaMsgImpl_setDestination (impl->mMamaBridgeMsg,
                                                   replyTo);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendReplyToInboxHandle(): "
                  "Could not set destination '%s' [%s]",
                  replyTo,
                  mamaStatus_stringForStatus (status));
        return status;
    }

    /* Fire out the message to the inbox */
    return nngBridgeMamaPublisher_send (publisher, reply);
}

mama_status
nngBridgeMamaPublisher_sendFromInbox (publisherBridge  publisher,
                                      mamaInbox        inbox,
                                      mamaMsg          msg)
{
    return nngBridgeMamaPublisher_sendFromInboxByIndex (publisher,
                                                         0,
                                                         inbox,
                                                         msg);
}

/* Send a message from the specified inbox using the throttle. */
mama_status
nngBridgeMamaPublisher_sendFromInboxByIndex (publisherBridge   publisher,
                                             int               tportIndex,
                                             mamaInbox         inbox,
                                             mamaMsg           msg)
{
    nngPublisherBridge*     impl        = (nngPublisherBridge*) publisher;
    const char*             replyAddr   = NULL;
    inboxBridge             inboxImpl   = NULL;
    mama_status             status      = MAMA_STATUS_OK;

    if (NULL == impl || NULL == inbox || NULL == msg)
    {
        return MAMA_STATUS_NULL_ARG;
    }

    /* Get the inbox which you want the publisher to respond to */
    inboxImpl = mamaInboxImpl_getInboxBridge (inbox);
    replyAddr = baseBridgeMamaInboxImpl_getReplySubject (inboxImpl);

    /* Mark this as being a request from an inbox */
    status = baseBridgeMamaMsgImpl_setMsgType (impl->mMamaBridgeMsg,
                                               BASE_MSG_INBOX_REQUEST);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendFromInboxByIndex(): "
                  "Failed to set message type [%s]",
                  mamaStatus_stringForStatus (status));
        return status;
    }

    /* Update meta data in outgoing message to reflect the inbox name */
    status = baseBridgeMamaMsgImpl_setInboxName (impl->mMamaBridgeMsg,
                                                 replyAddr);
    if (MAMA_STATUS_OK != status)
    {
        mama_log (MAMA_LOG_LEVEL_ERROR,
                  "nngBridgeMamaPublisher_sendFromInboxByIndex(): "
                  "Failed to set inbox name [%s]",
                  mamaStatus_stringForStatus (status));
        return status;
    }

    return nngBridgeMamaPublisher_send (publisher, msg);;
}

mama_status
nngBridgeMamaPublisher_setUserCallbacks (publisherBridge         publisher,
                                         mamaQueue               queue,
                                         mamaPublisherCallbacks* cb,
                                         void*                   closure)
{
    nngPublisherBridge*    impl        = (nngPublisherBridge*) publisher;

    if (NULL == impl || NULL == cb)
    {
        return MAMA_STATUS_NULL_ARG;
    }
    /* Take a copy of the callbacks */
    impl->mCallbacks = *cb;
    impl->mCallbackClosure = closure;

    return MAMA_STATUS_OK;
}

/*=========================================================================
 =                  Private implementation functions                     =
 =========================================================================*/

mama_status
nngBridgeMamaPublisherImpl_buildSendSubject (nngPublisherBridge* impl)
{
    char* keyTarget = NULL;

    /* If this is a special _MD publisher, lose the topic unless dictionary */
    if (impl->mRoot != NULL)
    {
        /*
         * May use strlen here to increase speed but would need to test to
         * verify this is the only circumstance in which we want to consider the
         * topic when a root is specified.
         */
        if (strcmp (impl->mRoot, "_MDDD") == 0)
        {
            nngBridgeMamaSubscriptionImpl_generateSubjectKey (impl->mRoot,
                                                              impl->mSource,
                                                              impl->mTopic,
                                                              &keyTarget);
        }
        else
        {
            nngBridgeMamaSubscriptionImpl_generateSubjectKey (impl->mRoot,
                                                              impl->mSource,
                                                              NULL,
                                                              &keyTarget);
        }
    }
    /* If this isn't a special _MD publisher */
    else
    {
        nngBridgeMamaSubscriptionImpl_generateSubjectKey (NULL,
                                                          impl->mSource,
                                                          impl->mTopic,
                                                          &keyTarget);
    }

    /* Set the subject for publishing here */
    impl->mSubject = keyTarget;

    return MAMA_STATUS_OK;
}
