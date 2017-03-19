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
#include <unistd.h>

enum _BTEventType
{
        /*
          client status change
         */
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

        /*
          device status change, have payload
         */
        BT_EVENT_DEVICE_OLD,   /* Used "devices" get the device
                                  connect <remote_device_MAC@> */

        BT_EVENT_DEVICE_NEW,   /* A new client and in our list
                                  pair <remote_device_MAC@>
                                  connect <remote_device_MAC@> */

        BT_EVENT_DEVICE_CONN,   /* Device 8C:DE:52:FB:C8:CE Connected: yes */
        BT_EVENT_DEVICE_DISCONN,/* Device 8C:DE:52:FB:C8:CE Connected: no  */

        BT_EVENT_DEVICE_CHG,   /*  */
        BT_EVENT_DEVICE_DEL,   /* */

        BT_EVENT_DEVICE_PID_CREATE, /* 8C:DE:52:FB:C8:CE PROCESS 798 */
        BT_EVENT_DEVICE_PID_CLOSE,  /* 8C:DE:52:FB:C8:CE CLOSEID 798 */

        /*
          device reconn with a timer (now use a 5s timer)
         */
        BT_EVENT_DEVICE_RECONN,

        /*
           pairing and connecting
        */
        BT_EVENT_REQ_PIN_CODE,
        BT_EVENT_REQ_PASS_KEY,
        BT_EVENT_REQ_CONFIRM,
        BT_EVENT_REQ_AUTH,
};

typedef enum _BTEventType BTEventType;

struct _BTEvent {
        BTEventType event_type;
        gpointer    payload;
};
typedef struct _BTEvent BTEvent;

struct _CMD {
        char    cmd[256];
};
typedef struct _CMD CMD;

/*
 * bt device type define
 */
enum BTTYPE {
	TYPE_NONE = 0,

	/*
	 *   device ok
	 */
	TYPE_RBP,               /* name RBP1601040168 */
	//TYPE_IDONN,           /* name X6S-159C  */
	TYPE_801B,              /* name SPO2:HC-801B */
        TYPE_601B,              /* name BG:HC-601B */
        TYPE_XINGXIAO,          /* xingxiao */
	TYPE_MODEM_NO_ANSWER,   /* NO ANSWER */
	TYPE_MODEM_BUSY,        /* BUSY */
	TYPE_MODEM_OK,          /* OK */
	TYPE_MODEM_ERROR,       /* ERROR */
	TYPE_MODEM_SMS,         /* +CMTI: "SM",xxx */

	TYPE_MODEM_MAX,         /* last modem event */


	/*
	 *    device don't run
	 */
	TYPE_SIP_CALLING = TYPE_MODEM_MAX + 1,     /* SIP_A -> SIP_B */
	TYPE_SIP_INCOMING,                        /* SIP_A <- SIP_B */
	TYPE_SIP_EARLY,
	TYPE_SIP_CONNECTING,
	TYPE_SIP_CONFIRMED,
	TYPE_SIP_DISCONNCTD,

	TYPE_SIP_MAX,          /* last sip event */

	/*
	 *    mqtt events
	 */
	TYPE_MQTT_CALLING = TYPE_SIP_MAX + 1,      /* Broker -> SIP_A (phone) */
	TYPE_MQTT_CALLED,                         /* SIP_A -> Broker */
	TYPE_MQTT_STATUS,                         /* Peer online or offline */
	TYPE_MQTT_SMS,                            /* Peer send SMS */

	TYPE_MQTT_MAX,          /* last mqtt event */

	TYPE_UNKNOW,
};

struct _Device {
        char address[18]; /* as hash key */
        char name[64];

        int  paired;
        int  trusted;
        int  blocked;
        int  connected;

        enum BTTYPE type;

        pid_t pid; /* per device a process ? */
};

typedef struct _Device Device;

void bt_event_free(BTEvent *event);

void bt_client_ready(GAsyncQueue *event_queue);
void bt_client_disconn(GAsyncQueue *event_queue);

void bt_device_old(GAsyncQueue *event_queue, Device *dev);
void bt_device_new(GAsyncQueue *event_queue, Device *dev);
void bt_device_conn(GAsyncQueue *event_queue, Device *dev);
void bt_device_disconn(GAsyncQueue *event_queue, Device *dev);
void bt_device_chg(GAsyncQueue *event_queue, Device *dev);
void bt_device_del(GAsyncQueue *event_queue, Device *dev);

void bt_device_process_create(GAsyncQueue *event_queue, Device *dev);
void bt_device_process_close(GAsyncQueue *event_queue, Device *dev);

void bt_device_reconn(GAsyncQueue *event_queue, Device *dev);

void bt_cmd_free(CMD *event);

#define NELEMS(array) (sizeof(array) / sizeof(array[0]))
void LOG(const char *fmt, ...);

#endif /* __EVENT_H__ */
