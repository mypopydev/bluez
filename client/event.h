/* bluetooth event
 *
 * event.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __EVENT_H__
#define __EVENT_H__

#include <glib.h>

enum _BTEventType
{
        BT_EVENT_CLIENT_READY, /* Client ready, connected to bluez with dbus
                                  power on
                                  agent on
                                  default-agent
                                  discoverable on
                                  pairable on
                                  scan on
                                  devices
                               */
        BT_EVENT_CLIENT_DISCONN, /* Can't connect with the dbus */

        BT_EVENT_DEVICE_OLD,   /* Used "devices" get the device */
        BT_EVENT_DEVICE_NEW,   /* A new client
                                  pair <remote_device_MAC@>
                                  connect <remote_device_MAC@> */
        BT_EVENT_DEVICE_CHG,   /* */
        BT_EVENT_DEVICE_DEL,   /* */
};

typedef enum _BTEventType BTEventType;

struct _BTEvent {
        BTEventType event_type;
        gpointer    payload;
};
typedef struct _BTEvent BTEvent;

struct _Device {
        char address[64];
        char name[64];

        int  paired;
        int  tructed;
        int  blocked;
        int  connected;
};

typedef struct _Device Device;


void bt_event_free (BTEvent *event);

void bt_client_ready (GAsyncQueue *event_queue);
void bt_client_disconn (GAsyncQueue *event_queue);

void bt_device_old(GAsyncQueue *event_queue, Device *dev);
void bt_device_new(GAsyncQueue *event_queue, Device *dev);
void bt_device_chg(GAsyncQueue *event_queue, Device *dev);
void bt_device_del(GAsyncQueue *event_queue, Device *dev);

#endif /* __EVENT_H__ */
