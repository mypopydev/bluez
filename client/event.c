/*
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <readline/readline.h>
#include <wordexp.h>

#include <glib.h>

#include "gdbus/gdbus.h"
#include "display.h"

#include "event.h"

void bt_event_free (BTEvent *event)
{
        if (event) {
                if (event->payload) {
                        switch (event->event_type) {
                            //case

                        }
                        //g_slice_free (GstRTPDTMFPayload, event->payload);
                }


                g_slice_free (BTEvent, event);
        }
}

static void
bt_add_start_event (GAsyncQueue *event_queue)
{

  BTEvent *event = g_slice_new0 (BTEvent);
  /*
  event->event_type = RTP_DTMF_EVENT_TYPE_START;

  event->payload = g_slice_new0 (GstRTPDTMFPayload);
  event->payload->event = CLAMP (event_number, MIN_EVENT, MAX_EVENT);
  event->payload->volume = CLAMP (event_volume, MIN_VOLUME, MAX_VOLUME);
  */

  g_async_queue_push (event_queue, event);
}

static void
bt_add_stop_event (GAsyncQueue *event_queue)
{

  BTEvent *event = g_slice_new0 (BTEvent);
  /*
  event->event_type = RTP_DTMF_EVENT_TYPE_STOP;
  */

  g_async_queue_push (event_queue, event);
}
