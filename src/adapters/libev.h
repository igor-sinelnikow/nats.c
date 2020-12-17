// Copyright 2020 The NATS Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIBEV_H_
#define LIBEV_H_

#ifdef __cplusplus
extern "C" {
#endif

/** \cond
 *
 */
#include <ev.h>
#include "../nats.h"

typedef struct
{
    natsConnection  *nc;
    struct ev_loop  *loop;
    ev_io           read;
    ev_io           write;
    ev_async        keepActive;

} natsLibevEvents;

/** \endcond
 *
 */


/** \defgroup libevFunctions Libev Adapter
 *
 *  Adapter to plug a `NATS` connection to a `libev` event loop.
 *  @{
 */

static void
natsLibev_ProcessEvent(struct ev_loop *loop, ev_io *w, int revents)
{
    natsLibevEvents *nle = (natsLibevEvents*) w->data;

    if (revents & EV_READ)
        natsConnection_ProcessReadEvent(nle->nc);

    if (revents & EV_WRITE)
        natsConnection_ProcessWriteEvent(nle->nc);
}

static void
keepAliveCb(struct ev_loop *loop, ev_async *w, int revents)
{
    // do nothing...
}

/** \brief Attach a connection to the given event loop.
 *
 * This callback is invoked after `NATS` library has connected, or reconnected.
 * For a reconnect event, `*userData` will not be `NULL`. This function will
 * start polling on READ events for the given `socket`.
 *
 * @param userData the location where the adapter stores the user object passed
 * to the other callbacks.
 * @param loop the event loop as a generic pointer. Cast to appropriate type.
 * @param nc the connection to attach to the event loop
 * @param socket the socket to start polling on.
 */
natsStatus
natsLibev_Attach(void **userData, void *loop, natsConnection *nc, natsSock socket)
{
    struct ev_loop  *libevLoop = (struct ev_loop*) loop;
    natsLibevEvents *nle       = (natsLibevEvents*) (*userData);

    // This is the first attach (when reconnecting, nle will be non-NULL).
    if (nle == NULL)
    {
        nle = (natsLibevEvents*) malloc(sizeof(natsLibevEvents));
        if (nle == NULL)
            return NATS_NO_MEMORY;

        nle->nc   = nc;
        nle->loop = libevLoop;

        ev_async_init(&nle->keepActive, keepAliveCb);
        ev_async_start(nle->loop, &nle->keepActive);

        ev_init(&nle->read, natsLibev_ProcessEvent);
        nle->read.data = (void*) nle;

        ev_init(&nle->write, natsLibev_ProcessEvent);
        nle->write.data = (void*) nle;
    }
    else
    {
        ev_io_stop(nle->loop, &nle->read);
        ev_io_stop(nle->loop, &nle->write);
    }

    ev_io_set(&nle->read, socket, EV_READ);
    ev_io_start(nle->loop, &nle->read);

    ev_io_set(&nle->write, socket, EV_WRITE);

    *userData = (void*) nle;

    return NATS_OK;
}

static void
ev_io_toggle(struct ev_loop *loop, ev_io *w, bool on)
{
    if (on)
        ev_io_start(loop, w);
    else
        ev_io_stop(loop, w);
}

/** \brief Start or stop polling on READ events.
 *
 * This callback is invoked to notify that the event library should start
 * or stop polling for READ events.
 *
 * @param userData the user object created in #natsLibev_Attach
 * @param add `true` if the library needs to start polling, `false` otherwise.
 */
natsStatus
natsLibev_Read(void *userData, bool add)
{
    natsLibevEvents *nle = (natsLibevEvents*) userData;
    ev_io_toggle(nle->loop, &nle->read, add);
    return NATS_OK;
}

/** \brief Start or stop polling on WRITE events.
 *
 * This callback is invoked to notify that the event library should start
 * or stop polling for WRITE events.
 *
 * @param userData the user object created in #natsLibev_Attach
 * @param add `true` if the library needs to start polling, `false` otherwise.
 */
natsStatus
natsLibev_Write(void *userData, bool add)
{
    natsLibevEvents *nle = (natsLibevEvents*) userData;
    ev_io_toggle(nle->loop, &nle->write, add);
    return NATS_OK;
}

/** \brief The connection is closed, it can be safely detached.
 *
 * When a connection is closed (not disconnected, pending a reconnect), this
 * callback will be invoked. This is the opportunity to cleanup the state
 * maintained by the adapter for this connection.
 *
 * @param userData the user object created in #natsLibev_Attach
 */
natsStatus
natsLibev_Detach(void *userData)
{
    natsLibevEvents *nle = (natsLibevEvents*) userData;

    ev_io_stop(nle->loop, &nle->read);
    ev_io_stop(nle->loop, &nle->write);
    ev_async_send(nle->loop, &nle->keepActive);
    ev_async_stop(nle->loop, &nle->keepActive);

    free(nle);

    return NATS_OK;
}

/** @} */ // end of libevFunctions

#ifdef __cplusplus
}
#endif

#endif /* LIBEV_H_ */
