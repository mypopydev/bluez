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
                        case BT_EVENT_DEVICE_OLD:
                        case BT_EVENT_DEVICE_NEW:
                        case BT_EVENT_DEVICE_CHG:
                        case BT_EVENT_DEVICE_DEL:
                                g_slice_free (Device, event->payload);
                                break;
                        default:
                                break;
                        }
                }

                g_slice_free (BTEvent, event);
        }
}

void bt_client_ready(GAsyncQueue *event_queue)
{

        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_CLIENT_READY;

        g_async_queue_push (event_queue, event);
}

void bt_client_disconn(GAsyncQueue *event_queue)
{

        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_CLIENT_DISCONN;

        g_async_queue_push(event_queue, event);
}

void bt_add_stop_event(GAsyncQueue *event_queue)
{

        BTEvent *event = g_slice_new0(BTEvent);

        g_async_queue_push (event_queue, event);
}

void bt_device_old(GAsyncQueue *event_queue, Device *dev)
{
        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_DEVICE_OLD;
        event->payload = dev;

        g_async_queue_push(event_queue, event);
}

void bt_device_new(GAsyncQueue *event_queue, Device *dev)
{
        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_DEVICE_NEW;
        event->payload = dev;

        g_async_queue_push(event_queue, event);
}

void bt_device_conn(GAsyncQueue *event_queue, Device *dev)
 {
        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_DEVICE_CONN;
        dev->connected = 1;
        event->payload = dev;

        g_async_queue_push(event_queue, event);
}

void bt_device_disconn(GAsyncQueue *event_queue, Device *dev)
{
        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_DEVICE_DISCONN;
        dev->connected = 0;
        event->payload = dev;

        g_async_queue_push (event_queue, event);
}

void bt_device_chg(GAsyncQueue *event_queue, Device *dev)
{
        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_DEVICE_CHG;
        event->payload = dev;

        g_async_queue_push (event_queue, event);
}

void bt_device_del(GAsyncQueue *event_queue, Device *dev)
{
        BTEvent *event = g_slice_new0(BTEvent);
        event->event_type = BT_EVENT_DEVICE_DEL;
        event->payload = dev;

        g_async_queue_push(event_queue, event);
}

void bt_cmd_free (CMD *event)
{
        if (event) {
                g_slice_free (CMD, event);
        }
}

#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

void LOG(const char *fmt, ...)
{
	char date[20];
	struct timeval tv;
	va_list args;

	/* print the timestamp */
	gettimeofday(&tv, NULL);
	strftime(date, NELEMS(date), "%Y-%m-%dT%H:%M:%S", localtime(&tv.tv_sec));
	printf("[%s.%03dZ] ", date, (int)tv.tv_usec/1000);

	/* printf like normal */
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
