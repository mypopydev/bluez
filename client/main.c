/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012  Intel Corporation. All rights reserved.
 *
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
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <wordexp.h>
#include <time.h>

//#include <readline/readline.h>
//#include <readline/history.h>
#include <glib.h>

#include "src/shared/util.h"
#include "gdbus/gdbus.h"
#include "monitor/uuid.h"
#include "agent.h"
#include "display.h"
#include "gatt.h"
#include "advertising.h"
#include "event.h"
#include "match.h"
#include "http_client.h"
#include "rl_protocol.h"
#include "utils.h"
//#include "attrib/utils.h"
//#include "gattlib.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>

/* String display constants */
#define COLORED_NEW	COLOR_GREEN "NEW" COLOR_OFF
#define COLORED_CHG	COLOR_YELLOW "CHG" COLOR_OFF
#define COLORED_DEL	COLOR_RED "DEL" COLOR_OFF

#define PROMPT_ON	COLOR_BLUE "[bluetooth]" COLOR_OFF "# "
#define PROMPT_OFF	"Waiting to connect to bluetoothd..."

static GMainLoop *main_loop;
static DBusConnection *dbus_conn;

static GAsyncQueue *async_queue = NULL;
static GThread *state_thread = NULL;

static GAsyncQueue *cmd_queue = NULL;
static GThread *cmd_thread = NULL;

static GDBusProxy *agent_manager;
static char *auto_register_agent = NULL;

struct adapter {
	GDBusProxy *proxy;
	GList *devices;
};

static char mac[13] = {0};

static struct adapter *default_ctrl;
static GDBusProxy *default_dev;
static GDBusProxy *default_attr;
static GDBusProxy *ad_manager;
static GList *ctrl_list;

static int server_fd;  /* Server <- Client */
static GIOChannel *server_io;

static int client_fd; /* Server -> Client */
static GIOChannel *client_io;

#define SERVER "/tmp/ud_bluetooth_main"
#define CLIENT "/tmp/ud_bluetooth_client"

GList *device_list = NULL;
GHashTable *device_hash = NULL;

#define MAX_THREADS 128
GThreadPool *thread_pool = NULL;

struct device_key {
        enum BTTYPE type;
        char regex[128];     /* match ours devices */
};

struct device_key device_keys[] = {
	{
		.type = TYPE_NONE,
		.regex = "unknown",
	},

	{
		.type = TYPE_RBP,
		.regex = "RBP",
	},

/*
	{
		.type = TYPE_IDONN,
		.regex = "X6S-",
	},
*/

	{
		.type = TYPE_801B,
		.regex = "HC-801B",
	},

	{
		.type = TYPE_601B,
		.regex = "HC-601B",
	},
};

struct http_response *send_data(int index, char *devmac, char *val);

static GAsyncQueue *dev_queue = NULL;
static GThread *dev_thread = NULL;

static enum BTTYPE find_bt_type(char *buf, int size, struct device_key *devices, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		if (match((char *)devices[i].regex, (char *)buf)) {
			return devices[i].type;
		}
	}

	return TYPE_NONE;
}


static guint input = 0;

static const char * const agent_arguments[] = {
	"on",
	"off",
	"DisplayOnly",
	"DisplayYesNo",
	"KeyboardDisplay",
	"KeyboardOnly",
	"NoInputNoOutput",
	NULL
};

static const char * const ad_arguments[] = {
	"on",
	"off",
	"peripheral",
	"broadcast",
	NULL
};

static int create_server_sock(char *name)
{
        struct sockaddr_un svaddr, claddr;
        int sfd, j;

        if (remove(name) == -1 && errno != ENOENT)
                LOG("remove unix sock data path\n");

        /* Create  socket; bind to unique pathname */
        sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (sfd == -1)
                LOG("create unix socket fail\n");

        memset(&claddr, 0, sizeof(struct sockaddr_un));
        claddr.sun_family = AF_UNIX;
        snprintf(claddr.sun_path, sizeof(claddr.sun_path),"%s", name);

        if (bind(sfd, (struct sockaddr *) &claddr, sizeof(struct sockaddr_un)) == -1)
                LOG("bind error\n");

        return sfd;
}

static int create_client_sock(char *name)
{
        struct sockaddr_un svaddr, claddr;
        int sfd, j;
        char path[128];

        snprintf(path, sizeof(path),
                 "%s.%ld", name, getpid());

        if (remove(path) == -1 && errno != ENOENT)
                LOG("remove unix sock data path\n");

        /* Create  socket; bind to unique pathname */
        sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (sfd == -1)
                LOG("create unix socket fail\n");

        memset(&claddr, 0, sizeof(struct sockaddr_un));
        claddr.sun_family = AF_UNIX;
        snprintf(&claddr.sun_path[1], sizeof(claddr.sun_path)-2,
                 "%s", path);
/*
        if (bind(sfd, (struct sockaddr *) &claddr, sizeof(struct sockaddr_un)) == -1)
                g_printerr("bind error\n");
*/

        return sfd;
}

int sock_send_cmd(int sock, char *client_path, char *cmd, int cmd_len)
{
        struct sockaddr_un svaddr;
        int len = sizeof(struct sockaddr_un);

        memset(&svaddr, 0, sizeof(struct sockaddr_un));
        svaddr.sun_family = AF_UNIX;
        strncpy(svaddr.sun_path, client_path, sizeof(svaddr.sun_path) - 1);

        if (sendto(sock, cmd, cmd_len, 0, (struct sockaddr *) &svaddr, len) != cmd_len)
                LOG("sendto");

        return 0;
}

int sock_send_client_cmd(int sock, char *client_path, char *cmd, int cmd_len)
{
        struct sockaddr_un svaddr;
        int len = sizeof(struct sockaddr_un);

        memset(&svaddr, 0, sizeof(struct sockaddr_un));
        svaddr.sun_family = AF_UNIX;
        strncpy(&svaddr.sun_path[1], client_path, sizeof(svaddr.sun_path) - 2);

        if (sendto(sock, cmd, cmd_len, 0, (struct sockaddr *) &svaddr, len) != cmd_len)
                LOG("sendto client error");

        return 0;
}

void print_key_value(gpointer key, gpointer value, gpointer user_data)
{
	LOG("%s \n", key);
	Device *dev = value;
	LOG("    %s %s \n    paired: %d trusted: %d blocked: %d connected: %d type: %d pid: %d\n" ,
	       dev->address, dev->name, dev->paired, dev->trusted, dev->blocked, dev->connected, dev->type, dev->pid);
}

void display_hash_table(GHashTable *table)
{
        LOG("\n");
	LOG("Hash star ---> \n");
	g_hash_table_foreach(table, print_key_value, NULL);
	LOG("Hash end  ---> \n");
}

static void power_on()
{
        CMD *event = g_slice_new0 (CMD);
        snprintf(event->cmd, 255, "power on");
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void agent_on()
{
        CMD *event = g_slice_new0 (CMD);
        snprintf(event->cmd, 255, "agent on");
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
        //sleep(2);
}

static void default_agent()
{
        CMD *event = g_slice_new0 (CMD);
        snprintf(event->cmd, 255, "default-agent");
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void discoverable_on()
{
        CMD *event = g_slice_new0 (CMD);
        snprintf(event->cmd, 255, "discoverable on");
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void pairable_on()
{
        CMD *event = g_slice_new0 (CMD);
	snprintf(event->cmd, 255, "pairable on");
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void scan_on()
{
        CMD *event = g_slice_new0 (CMD);
	snprintf(event->cmd, 255, "scan on");
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void list_devices()
{
        CMD *event = g_slice_new0 (CMD);
	snprintf(event->cmd, 255, "devices");
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void pair_device(char *bdaddr)
{
        CMD *event = g_slice_new0 (CMD);
	snprintf(event->cmd, 255, "pair %s", bdaddr);
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void trust_device(char *bdaddr)
{
        CMD *event = g_slice_new0 (CMD);
	snprintf(event->cmd, 255, "trust %s", bdaddr);
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
}

static void connect_device(char *bdaddr, int type)
{
        /*
        CMD *event = g_slice_new0 (CMD);
	snprintf(event->cmd, 255, "connect %s", bdaddr);
        LOG("S -> C: %s\n", event->cmd);
        g_async_queue_push (cmd_queue, event);
        */
        char cmd[128] = {0};
        if (type == TYPE_RBP)
                snprintf(cmd, strlen(bdaddr)+strlen("connect ")+2+strlen("type")+2,
                         "connect %s type %d", bdaddr, type);
        else if (type == TYPE_601B)
                snprintf(cmd, strlen(bdaddr)+strlen("connect 601B ")+1,
                         "connect %s 601B", bdaddr);
        else
                snprintf(cmd, strlen(bdaddr)+strlen("connect ")+1, "connect %s", bdaddr);

        LOG("[Sock] S -> C: %s\n", cmd);

        sock_send_cmd(client_fd, CLIENT, cmd, strlen(cmd));

        /* XXX: notify the server, work for bluetooth device  */
        struct http_response *resp = send_data(type, bdaddr, "btnew");
        http_response_free(resp);
}

static Device *find_device_by_address(GHashTable *hash_table, const char *address)
{
        if (address)
                return g_hash_table_lookup(hash_table, address);

        return NULL;
}

gboolean match_device_name(gpointer key, gpointer value, gpointer user_data)
{
        char *name = (char *)user_data;
        Device *device = value;
        return  match(name, (char *)device->name);
}

static int find_device_by_name(GHashTable *hash_table, const char *name)
{
        /*
        GHashTableIter iter;
        gpointer key, value;
        char *addr = key;
        Device *device = value;

        g_hash_table_iter_init(&iter, hash_table);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
                if (device && device->name && match((char *)name, (char *)device->name))
			return device;
        }
        */
        Device *device = NULL;
        device = g_hash_table_find(hash_table, match_device_name, name);

        return device;
}

gboolean match_device_pid(gpointer key, gpointer value, gpointer user_data)
{
        int pid = *((int *)user_data);
        Device *device = value;
        return  (pid == device->pid);
}

static Device *find_device_by_pid(GHashTable *hash_table, int *pid)
{
        Device *device = NULL;
        device = g_hash_table_find(hash_table, match_device_pid, pid);

        return device;
}


void reconn_device(gpointer key, gpointer value, gpointer user_data)
{
	LOG("%s \n", key);
	Device *dev = value;
        GError *gerr = NULL;
	GIOChannel *chan;

        //if (dev && dev->connected != 1 && dev->type != TYPE_RBP) {
        if (dev && dev->connected != 1) {
                //connect_device(dev->address);
            /*
                chan = gatt_connect("hci0", dev->address, "public", "low",
                                    0, 0, connect_cb, &gerr);
                if (chan == NULL) {
                        g_printerr("=======%s\n", gerr->message);
                        g_clear_error(&gerr);
                }
            */
        }
	LOG("\n    %s %s \n    paired: %d trusted: %d blocked: %d connected: %d type: %d pid: %d\n" ,
	       dev->address, dev->name, dev->paired, dev->trusted, dev->blocked, dev->connected, dev->type, dev->pid);
}

void reconn_devices(GHashTable *table)
{
	g_hash_table_foreach(table, reconn_device, NULL);
}


/* static void reconn_devices(GHashTable *hash_table) */
/* { */
/*         GHashTableIter iter; */
/*         gpointer key, value; */
/*         char *addr = key; */
/*         Device *device = value; */

/*         g_hash_table_iter_init(&iter, hash_table); */
/*         while (g_hash_table_iter_next(&iter, &key, &value)) { */
/*                 /\* */
/*                 if (device->connected != 1) */
/* 			connect_device(device->address); */
/*                 *\/ */
/*         } */

/*         return; */
/* } */

void release_key(gpointer data)
{
        char *address = data;
        free(address);
}

void release_value(gpointer data)
{
        Device *dev = data;
        g_slice_free (Device, dev);
}


#define META_KEY "APP201600000XD8E"
//#define URL  "http://120.24.159.138:8820/"
//#define URL_INTFACE "deviceInterface/i.ashx?"
char URL[128] = {0};
#define URL_INTFACE "/i.ashx?"

//#define CONFIG_FILE "/root/bluetoothctl.cfg"
//#define CONFIG_FILE "/home/barry/Project/bluez-dev/client/bluetoothctl.cfg"
//#define CONFIG_FILE  "/home/pi/Project/bluez-dev/client/bluetoothctl.cfg"
#define CONFIG_FILE "/home/media/Study/bluez-dev/client/bluetoothctl.cfg"

struct http_response *self_check()
{
        char url[1024] = {0};
        struct http_response *http_resp = NULL;
        time_t cur_time = time(NULL);
        char *enc = NULL;
        char cmd[128] = {0};

        char date[20];
	struct timeval tv;

	/* print the timestamp */
	gettimeofday(&tv, NULL);
	strftime(date, NELEMS(date), "%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
	LOG("[%s.%03dZ] ", date, (int)tv.tv_usec/1000);

        snprintf(cmd, 127, "{t=%s;}", date);
        enc = g_base64_encode(cmd, strlen(cmd));
        snprintf(url, 1023, "%s%sg=%s&a=01&s=%ld&p=%s", URL,URL_INTFACE, mac, cur_time, enc);
        http_resp = http_get(url, NULL);
        g_free(enc);
        if (http_resp) {
                LOG("header %s\n", http_resp->request_headers);
                LOG("body %s\n", http_resp->body);
        }
        return http_resp;
}

struct http_response *send_data(int index, char *devmac, char *val)
{
        char url[1024] = {0};
        struct http_response *http_resp = NULL;
        time_t cur_time = time(NULL);
        char *enc = NULL;
        char cmd[128] = {0};

        char date[20];
	struct timeval tv;

	/* print the timestamp */
	gettimeofday(&tv, NULL);
	strftime(date, NELEMS(date), "%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
	LOG("[%s.%03dZ] ", date, (int)tv.tv_usec/1000);

        /* n(int),e(String),t(Long),d(String);n,e,t,d */
        snprintf(cmd, 127, "{n=%d,e=%s,t=%s,d=%s;}", index, devmac, date, val);
        enc = g_base64_encode(cmd, strlen(cmd));
        snprintf(url, 1023, "%s%sg=%s&a=02&s=%ld&p=%s", URL,URL_INTFACE, mac, cur_time, enc);
        http_resp = http_get(url, NULL);
        LOG("URL %s\n", url);
        g_free(enc);
        if (http_resp) {
                LOG("header %s\n", http_resp->request_headers);
                LOG("body %s\n", http_resp->body);
        }

        return http_resp;
}

struct http_response *close_device()
{
        char url[1024] = {0};
        struct http_response *http_resp = NULL;
        time_t cur_time = time(NULL);
        char *enc = NULL;
        char cmd[128] = {0};

        char date[20];
	struct timeval tv;

	/* print the timestamp */
	gettimeofday(&tv, NULL);
	strftime(date, NELEMS(date), "%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
	LOG("[%s.%03dZ] ", date, (int)tv.tv_usec/1000);

        snprintf(cmd, 127, "{t=%s;}", date);
        enc = g_base64_encode(cmd, strlen(cmd));
        snprintf(url, 1023, "%s%sg=%s&a=03&s=%ld&p=%s", URL,URL_INTFACE, mac, cur_time, enc);
        http_resp = http_get(url, NULL);
        g_free(enc);
        if (http_resp) {
                LOG("header %s\n", http_resp->request_headers);
                LOG("body %s\n", http_resp->body);
        }

        return http_resp;
}

struct http_response *init_device(int inited)
{
        char url[1024] = {0};
        struct http_response *http_resp = NULL;
        time_t cur_time = time(NULL);
        char *enc = NULL;
        char cmd[128] = {0};

        snprintf(cmd, 127, "{s=%d;}", !!inited);
        enc = g_base64_encode(cmd, strlen(cmd));
        snprintf(url, 1023, "%s%sg=%s&a=04&s=%ld&p=%s", URL,URL_INTFACE, mac, cur_time, enc);
        http_resp = http_get(url, NULL);
        g_free(enc);
        if (http_resp) {
                LOG("header %s\n", http_resp->request_headers);
                LOG("body %s\n", http_resp->body);
        }

        return http_resp;
}

struct http_response *fail_device(int index, char *devmac, char *reason)
{
        char url[1024] = {0};
        struct http_response *http_resp = NULL;
        time_t cur_time = time(NULL);
        char *enc = NULL;
        char cmd[128] = {0};
        //int index = 1;
        char date[20];
	struct timeval tv;

	/* print the timestamp */
	gettimeofday(&tv, NULL);
	strftime(date, NELEMS(date), "%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
	LOG("[%s.%03dZ] ", date, (int)tv.tv_usec/1000);

        snprintf(cmd, 127, "{n=%d,e=%s,t=%s,f=%s;}", index, devmac, date, reason); /* n(int),e(String),t(Long),f(String);n,e,t,f */
        enc = g_base64_encode(cmd, strlen(cmd));
        snprintf(url, 1023, "%s%sg=%s&a=05&s=%ld&p=%s", URL,URL_INTFACE, mac, cur_time, enc);
        http_resp = http_get(url, NULL);
        g_free(enc);
        if (http_resp) {
                LOG("header %s\n", http_resp->request_headers);
                LOG("body %s\n", http_resp->body);
        }

        return http_resp;
}

struct http_response *version_check(char *devmac, char *reason)
{
        char url[1024] = {0};
        struct http_response *http_resp = NULL;
        time_t cur_time = time(NULL);
        char *enc = NULL;
        char cmd[128] = {0};

        char date[20];
	struct timeval tv;

	/* print the timestamp */
	gettimeofday(&tv, NULL);
	strftime(date, NELEMS(date), "%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
	LOG("[%s.%03dZ] ", date, (int)tv.tv_usec/1000);

        snprintf(cmd, 127, "{e=%s,t=%s,n=%s;}", devmac, date, reason); /* e(String),t(Long),n(String);e,t,n */
        enc = g_base64_encode(cmd, strlen(cmd));
        snprintf(url, 1023, "%s%sg=%s&a=06&s=%ld&p=%s", URL,URL_INTFACE, mac, cur_time, enc);
        http_resp = http_get(url, NULL);
        g_free(enc);
        if (http_resp) {
                LOG("header %s\n", http_resp->request_headers);
                LOG("body %s\n", http_resp->body);
        }

        return http_resp;
}

struct http_response *update_fw(char *devmac, char *reason)
{
        char url[1024] = {0};
        struct http_response *http_resp = NULL;
        time_t cur_time = time(NULL);
        char *enc = NULL;
        char cmd[128] = {0};
        int index = 1;

        snprintf(cmd, 127, "{e=%s,t=%ld, n=%s;}", devmac, cur_time, reason); /* e(String),t(Long),n(String);e,t,n */
        enc = g_base64_encode(cmd, strlen(cmd));
        snprintf(url, 1023, "%s%sg=%s&a=07&s=%ld&p=%s", URL,URL_INTFACE, mac, cur_time, enc);
        http_resp = http_get(url, NULL);
        g_free(enc);

        if (http_resp) {
                LOG("header %s\n", http_resp->request_headers);
                LOG("body %s\n", http_resp->body);
        }

        return http_resp;
}


static gpointer state_handle(gpointer data)
{
        GAsyncQueue *async_queue = data;
        BTEvent *event;
        Device *dev = NULL;
        Device *device = NULL;
        char *address = NULL;
        enum BTTYPE dev_type = TYPE_NONE;
        struct http_response *http_resp = NULL;
        int len = 0;


        while (event = g_async_queue_pop (async_queue)) {
#if 0
        for (;;) {
        retry:
                len = g_async_queue_length (async_queue);
                if (len <= 0) {
                        usleep(500000);
                        goto retry;
                }
                event = g_async_queue_pop (async_queue);
#endif
                switch (event->event_type) {
                case BT_EVENT_CLIENT_READY:
                        power_on();
                        agent_on();
                        default_agent();
                        discoverable_on();
                        pairable_on();
                        scan_on();
                        list_devices();
                        break;

                case BT_EVENT_DEVICE_OLD:
                        LOG(" <<<<<< OLD >>>>>>\n");
                        dev = event->payload;
                        dev_type = find_bt_type(dev->name,
                                                strlen(dev->name),
                                                device_keys,
                                                NELEMS(device_keys));
                        if ((dev_type != TYPE_NONE) && find_device_by_name(device_hash, dev->name)) {
                                /* XXX: find this device in hash table */
                                dev->type = dev_type;
                                device = g_slice_dup(Device, dev);
                                device->pid = -1;
                                address = strndup(dev->address, 17);
                                g_hash_table_insert(device_hash,
                                                    address,
                                                    device);
				display_hash_table(device_hash);
                                switch (dev_type) {
                                case TYPE_RBP:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_801B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_601B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        //pair_device(address);
                                        break;
                                default:
                                        break;
                                }
                        }
                        display_hash_table(device_hash);
                        LOG(" <<<<<< OLD >>>>>>\n");
                        break;

                case BT_EVENT_DEVICE_NEW:
                        LOG(" <<<<<< NEW >>>>>>\n");
                        dev = event->payload;
                        dev_type = find_bt_type(dev->name,
                                                strlen(dev->name),
                                                device_keys,
                                                NELEMS(device_keys));
                        if (dev_type != TYPE_NONE) {
                                dev->type = dev_type;
                                device = g_slice_dup(Device, dev);
                                device->pid = -1;
                                address = strndup(dev->address, 17);
                                g_hash_table_insert(device_hash,
                                                    address,
                                                    device);
				display_hash_table(device_hash);
                                switch (dev_type) {
                                case TYPE_RBP:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_801B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_601B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        //pair_device(address);
                                        break;
                                default:
                                        break;
                                }
                        }
                        display_hash_table(device_hash);
                        LOG(" <<<<<< NEW >>>>>>\n");
                        break;

                case BT_EVENT_DEVICE_DEL:
                        LOG(" <<<<<< DEL >>>>>>\n");
                        dev = event->payload;
                        g_hash_table_remove(device_hash,
                                            dev->address);

			display_hash_table(device_hash);
                        LOG(" <<<<<< DEL >>>>>>\n");
                        break;

                case BT_EVENT_DEVICE_CHG:
                        /* Update the device status ? */
                        LOG(" <<<<<< CHG >>>>>>\n");
                        dev = event->payload;
                        dev_type = find_bt_type(dev->name,
                                                strlen(dev->name),
                                                device_keys,
                                                NELEMS(device_keys));
                        device = g_hash_table_lookup(device_hash, dev->address);
                        if (dev_type != TYPE_NONE && !device) {
                                /* insert this device in hash table and connect */
                                dev->type = dev_type;
                                device = g_slice_dup(Device, dev);
                                device->pid = -1;
                                address = strndup(dev->address, 17);
                                g_hash_table_insert(device_hash,
                                                    address,
                                                    device);
				display_hash_table(device_hash);
                                switch (dev_type) {
                                case TYPE_RBP:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_801B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_601B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        //pair_device(address);
                                        break;
                                default:
                                        break;
                                }
                        } else if (dev_type != TYPE_NONE && device && device->pid == -1) {
                                /* the device in list but is not connected */
                                switch (dev_type) {
                                case TYPE_RBP:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_801B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        break;
                                case TYPE_601B:
                                        trust_device(address);
                                        connect_device(address, dev_type);
                                        //pair_device(address);
                                        break;
                                default:
                                        break;
                                }
                        }
			display_hash_table(device_hash);
                        LOG(" <<<<<< CHG >>>>>>\n");
                        break;

                case BT_EVENT_DEVICE_RECONN:
                        /* timeout, try to re-connect the device with a 5s timer
                           in device hash */
                        //reconn_devices(device_hash);
                        break;

                case BT_EVENT_DEVICE_CONN:
                        /* device connected, update hash table, try to read the value and
                           send the result to server */
                        LOG(" <<<<<< CONN >>>>>>\n");
                        dev = event->payload;
                        device = g_hash_table_lookup(device_hash, dev->address);
                        if (device) {
                                device->connected = 1;
                                dev_type = device->type;
                        } else {
                                /* can't find the device, but we will
                                   try to insert to hash table */
                                dev_type = find_bt_type(dev->name,
                                                strlen(dev->name),
                                                device_keys,
                                                NELEMS(device_keys));
                                if (dev_type != TYPE_NONE) {
                                        dev->type = dev_type;
                                        device = g_slice_dup(Device, dev);
                                        address = strndup(dev->address, 17);
                                        device->connected = 1;
                                        g_hash_table_insert(device_hash,
                                                            address,
                                                            device);
                                }
                        }
                        switch (dev_type) {
                        case TYPE_RBP:
                                /* XXX: get CONN event from bluez daemo ? */
                                /* wait cmd and recv the data */
                                break;

                        case TYPE_801B:
                                /*
                                 *  wait cmd and recv the data
                                 *  register-notify 0x35
                                 */
                        {
                                char client[128] = { 0 };
                                char cmd[128] = { 0 };
                                snprintf(&client[0], 127, "%s.%d", CLIENT, device->pid);
                                snprintf(cmd, strlen("register-notify 0x35\n"), "register-notify 0x35\n");
                                sock_send_client_cmd(client_fd, client, cmd, strlen(cmd)+1);
                        }
                                break;

                        case TYPE_601B:
                                /* wait cmd and recv the data */
                                /*
                                 *  wait cmd and recv the data
                                 *  register-notify 0x12
                                 */
                        {
                                //pair_device(dev->address);
                                char client[128] = { 0 };
                                char cmd[128] = { 0 };
                                snprintf(&client[0], 127, "%s.%d", CLIENT, device->pid);

                                memset(cmd, 0, 128);

                                //snprintf(cmd, strlen("set-sign-key -c 1234\n"), "set-sign-key -c 1234\n");
                                //sock_send_client_cmd(client_fd, client, cmd, strlen(cmd) + 1);

                                //snprintf(cmd, strlen("register-notify 0x13")+1, "register-notify 0x13");
                                /*
                                snprintf(cmd, strlen("write-value -w 0x13 00 01 00\n"), "write-value -w 0x13 00 01 00\n");
                                sock_send_client_cmd(client_fd, client, cmd, strlen(cmd) + 1);

                                memset(cmd, 0, 128);
                                snprintf(cmd, strlen("write-value -w 0x1b 00 02 00\n"), "write-value -w 0x1b 00 01 01\n");
                                sock_send_client_cmd(client_fd, client, cmd, strlen(cmd) + 1);
                                */

                                memset(cmd, 0, 128);
                                snprintf(cmd, strlen("register-notify 0x12\n"), "register-notify 0x12\n");
                                //sock_send_client_cmd(client_fd, client, cmd, strlen(cmd) + 1);

                                 /* XXX: FIXMEM Workround send data to server */
                                /*
                                 uint8_t value[128] =  {0};
                                 unsigned int value1;
                                 rand_r(&value1);
                                 snprintf(value, 127, "%.2f", (value1%60 + 40)/10.0);
                                 struct http_response *resp = send_data(TYPE_601B, dev->address, value);
                                 http_response_free(resp);
                                */
                        }
                                break;

                        default:
                                break;
                        }
			display_hash_table(device_hash);
                        LOG(" <<<<< CONN >>>>>>\n");
                        break;

                case BT_EVENT_DEVICE_DISCONN:
                        /* Update the device connect status */
                        LOG(" <<<<<< DISCONN >>>>>>\n");
                        dev = event->payload;
                        device = g_hash_table_lookup(device_hash, dev->address);
                        if (device) {
                                device->connected = 0;

                                switch (device->type) {
                                case TYPE_RBP:
                                        break;

                                case TYPE_801B:
                                        break;

                                case TYPE_601B:
                                        break;

                                default:
                                        break;
                                }
                        }
                        display_hash_table(device_hash);
                        LOG(" <<<<<< DISCONN >>>>>>\n");
                        break;

                case BT_EVENT_DEVICE_PID_CREATE:
                        /* Update the process status */
                        LOG(" <<<<<< PROCESS >>>>>>\n");
                        dev = event->payload;
                        device = g_hash_table_lookup(device_hash, dev->address);
                        if (device) {
                                device->pid = dev->pid;
                                switch (device->type) {
                                case TYPE_RBP:
                                        break;

                                case TYPE_801B:
                                        break;

                                case TYPE_601B:
                                        break;

                                default:
                                        break;
                                }
                        }
                        display_hash_table(device_hash);
                        LOG(" <<<<<< PROCESS >>>>>>\n");
                        break;

                case BT_EVENT_DEVICE_PID_CLOSE:
                        /* Update the process/connect status */
                        LOG(" <<<<<< CLOSE >>>>>>\n");
                        dev = event->payload;
                        device = find_device_by_pid(device_hash, &dev->pid);
                        if (device) {
                                device->connected = 0;
                                device->pid = -1;

                                switch (device->type) {
                                case TYPE_RBP:
                                        break;

                                case TYPE_801B:
                                        break;

                                case TYPE_601B:
                                        break;

                                default:
                                        break;
                                }
                        }
                        display_hash_table(device_hash);
                        LOG(" <<<<<< CLOSE >>>>>>\n");
                default:
                        break;
                }

                bt_event_free (event);
        }

        return NULL;
}

static gboolean
recurser_start (gpointer data)
{
    //LOG("Device re-connect now !!!\n");
        bt_device_reconn(async_queue, NULL);
        return TRUE;
}

static void proxy_leak(gpointer data)
{
	LOG("Leaking proxy %p\n", data);
}

static gboolean input_handler(GIOChannel *channel, GIOCondition condition,
							gpointer user_data)
{
	if (condition & G_IO_IN) {
		//rl_callback_read_char();
		return TRUE;
	}

	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		g_main_loop_quit(main_loop);
		return FALSE;
	}

	return TRUE;
}

static guint setup_standard_input(void)
{
	GIOChannel *channel;
	guint source;

	channel = g_io_channel_unix_new(fileno(stdin));

	source = g_io_add_watch(channel,
				G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
				input_handler, NULL);

	g_io_channel_unref(channel);

	return source;
}

static void connect_handler(DBusConnection *connection, void *user_data)
{
    /*
	rl_set_prompt(PROMPT_ON);
	printf("\r");
	rl_on_new_line();
	rl_redisplay();
    */

        //bt_client_ready(async_queue);
}

static void disconnect_handler(DBusConnection *connection, void *user_data)
{
	if (input > 0) {
		g_source_remove(input);
		input = 0;
	}
/*
	rl_set_prompt(PROMPT_OFF);
	printf("\r");
	rl_on_new_line();
	rl_redisplay();
*/

	g_list_free_full(ctrl_list, proxy_leak);
	ctrl_list = NULL;

	default_ctrl = NULL;
}

/* receive the message from client */
static gboolean server_handler(GIOChannel *channel, GIOCondition condition,
                               gpointer user_data)
{
        char buf[128];
        ssize_t numBytes;
        struct sockaddr_un claddr;
        socklen_t len;
	if (condition & G_IO_IN) {
                 len = sizeof(struct sockaddr_un);
                 numBytes = recvfrom(server_fd, buf, 128, 0,
                                     (struct sockaddr *) &claddr, &len);
                 if (numBytes == -1) {
                         LOG("recvfrom erron in server handler.\n");
                         usleep(500000);
                         return TRUE;
                 }

                 LOG("Server received %ld bytes from %s\n", (long) numBytes,
                        claddr.sun_path);

                 /*
                 for (j = 0; j < numBytes; j++)
                         buf[j] = toupper((unsigned char) buf[j]);
                 */
                 LOG(" C-> S : %s\n", buf);

                 if (match("CONNECT", buf)) {
                         Device *dev = g_slice_new0(Device);
                         snprintf(dev->address, sizeof(dev->address), "%s", buf);
                         dev->connected = 1;
                         bt_device_conn(async_queue, dev);
                 } else if (match("DISCONN", buf)) {
                         Device *dev = g_slice_new0(Device);
                         snprintf(dev->address, sizeof(dev->address), "%s", buf);
                         dev->connected = 0;
                         bt_device_disconn(async_queue, dev);
                 } else if (match("PROCESS", buf)) {
                         Device *dev = g_slice_new0(Device);
                         snprintf(dev->address, sizeof(dev->address), "%s", buf);
                         char *pid = strstr(buf, "PROCESS");
                         dev->pid = atoi(pid+8);
                         bt_device_process_create(async_queue, dev);
                 } else if (match("CLOSEID", buf)) {
                         Device *dev = g_slice_new0(Device);
                         //snprintf(dev->address, sizeof(dev->address), "%s", buf);
                         dev->pid = atoi(buf+8);
                         LOG("Destory pid : %d\n", dev->pid);
                         bt_device_process_close(async_queue, dev);
                 } else if (match("DATA", buf) && match(";", buf)) {
                         char address[18] = {0};
                         char value[64] = {0};
                         memcpy(address, buf, 17);
                         char *tmp = strstr(buf, "DATA");
                         if (tmp)
                                 snprintf(value, 63, "%s", tmp+5);
                         value[strlen(value)] = 0;
                         Device *dev = find_device_by_address(device_hash, address);
                         if (dev) {
                                 /* XXX: send data to server */
                                 struct http_response *resp = send_data(dev->type, address, value);
                                 http_response_free(resp);
                         } else {
                                 printf("Can't find the device %s\n", address);
                         }
                 } else if (match("SNIFFER", buf)) {
                         char address[18] = {0};
                         char value[64] = {0};
                         memcpy(address, buf, 17);
                         char *tmp = strstr(buf, "SNIFFER");
                         /* XXX: send data to server */
                         struct http_response *resp = send_data(TYPE_XINGXIAO, address, "sniffer");
                         http_response_free(resp);
                 }
                 return TRUE;
	}

	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		g_main_loop_quit(main_loop);
		return FALSE;
	}

	return TRUE;
}

static void print_adapter(GDBusProxy *proxy, const char *description)
{
	DBusMessageIter iter;
	const char *address, *name;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	LOG("%s%s%sController %s %s %s\n",
				description ? "[" : "",
				description ? : "",
				description ? "] " : "",
				address, name,
				default_ctrl &&
				default_ctrl->proxy == proxy ?
				"[default]" : "");

        bt_client_ready(async_queue);

}

static void print_device(GDBusProxy *proxy, const char *description)
{
	DBusMessageIter iter;
	const char *address, *name;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	LOG("%s%s%sDevice %s %s\n",
				description ? "[" : "",
				description ? : "",
				description ? "] " : "",
				address, name);

        /* handle the device list */
        Device *dev = g_slice_new0(Device);
        snprintf(dev->name, sizeof(dev->name), "%s", name);
        snprintf(dev->address, sizeof(dev->address), "%s", address);
        if (description == NULL) {
                bt_device_old(async_queue, dev);
        } else if (match((char *)"NEW", (char *)description)) {
                bt_device_new(async_queue, dev);
        } else {
                bt_device_del(async_queue, dev);
        }
}

static void print_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t valbool;
	dbus_uint32_t valu32;
	dbus_uint16_t valu16;
	dbus_int16_t vals16;
	unsigned char byte;
	const char *valstr;
	DBusMessageIter subiter;
	char *entry;
        LOG("%s/%s\n", label, name);
	if (iter == NULL) {
		LOG("%s%s is nil\n", label, name);
		return;
	}

        /* XXX: FIXME */
        /*
        if (match((char *)"Device", (char *)label) && !(match((char *)"Connected", (char *)name)))
                return;
        */

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		LOG("%s%s is invalid\n", label, name);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		LOG("%s%s: %s\n", label, name, valstr);
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		LOG("%s %s: %s\n", label, name,
					valbool == TRUE ? "yes" : "no");
                if (match((char *)"Connected", (char *)name) && match((char *)"Device", (char *)label)) {
                        Device *dev = g_slice_new0(Device);
                        snprintf(dev->address, sizeof(dev->address), "%s", label+strlen("Device "));
                        if (valbool == TRUE) {
                                dev->connected = 1;
                                bt_device_conn(async_queue, dev);
                        } else {
                                dev->connected = 0;
                                bt_device_disconn(async_queue, dev);
                        }
                }
                LOG("%s %s is XXX\n", label, name);
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		LOG("%s%s: 0x%06x\n", label, name, valu32);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
		LOG("%s%s: 0x%04x\n", label, name, valu16);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		LOG("%s%s: %d\n", label, name, vals16);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &byte);
		LOG("%s%s: 0x%02x\n", label, name, byte);
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		print_iter(label, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);
		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			print_iter(label, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		print_iter(label, entry, &subiter);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		print_iter(label, entry, &subiter);
		g_free(entry);
		break;
	default:
		LOG("%s%s has unsupported type\n", label, name);
		break;
	}
}

static void print_property(GDBusProxy *proxy, const char *name)
{
	DBusMessageIter iter;

	if (g_dbus_proxy_get_property(proxy, name, &iter) == FALSE)
		return;

	print_iter("\t", name, &iter);
}

static void print_uuids(GDBusProxy *proxy)
{
	DBusMessageIter iter, value;

	if (g_dbus_proxy_get_property(proxy, "UUIDs", &iter) == FALSE)
		return;

	dbus_message_iter_recurse(&iter, &value);

	while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
		const char *uuid, *text;

		dbus_message_iter_get_basic(&value, &uuid);

		text = uuidstr_to_str(uuid);
		if (text) {
			char str[26];
			unsigned int n;

			str[sizeof(str) - 1] = '\0';

			n = snprintf(str, sizeof(str), "%s", text);
			if (n > sizeof(str) - 1) {
				str[sizeof(str) - 2] = '.';
				str[sizeof(str) - 3] = '.';
				if (str[sizeof(str) - 4] == ' ')
					str[sizeof(str) - 4] = '.';

				n = sizeof(str) - 1;
			}

			LOG("\tUUID: %s%*c(%s)\n",
						str, 26 - n, ' ', uuid);
		} else
			LOG("\tUUID: %*c(%s)\n", 26, ' ', uuid);

		dbus_message_iter_next(&value);
	}
}

static gboolean device_is_child(GDBusProxy *device, GDBusProxy *master)
{
	DBusMessageIter iter;
	const char *adapter, *path;

	if (!master)
		return FALSE;

	if (g_dbus_proxy_get_property(device, "Adapter", &iter) == FALSE)
		return FALSE;

	dbus_message_iter_get_basic(&iter, &adapter);
	path = g_dbus_proxy_get_path(master);

	if (!strcmp(path, adapter))
		return TRUE;

	return FALSE;
}

static gboolean service_is_child(GDBusProxy *service)
{
	GList *l;
	DBusMessageIter iter;
	const char *device, *path;

	if (g_dbus_proxy_get_property(service, "Device", &iter) == FALSE)
		return FALSE;

	dbus_message_iter_get_basic(&iter, &device);

	if (!default_ctrl)
		return FALSE;

	for (l = default_ctrl->devices; l; l = g_list_next(l)) {
		struct GDBusProxy *proxy = l->data;

		path = g_dbus_proxy_get_path(proxy);

		if (!strcmp(path, device))
			return TRUE;
	}

	return FALSE;
}

static struct adapter *find_parent(GDBusProxy *device)
{
	GList *list;

	for (list = g_list_first(ctrl_list); list; list = g_list_next(list)) {
		struct adapter *adapter = list->data;

		if (device_is_child(device, adapter->proxy) == TRUE)
			return adapter;
	}
	return NULL;
}

static void set_default_device(GDBusProxy *proxy, const char *attribute)
{
	char *desc = NULL;
	DBusMessageIter iter;
	const char *path;

	default_dev = proxy;

	if (proxy == NULL) {
		default_attr = NULL;
		goto done;
	}

	if (!g_dbus_proxy_get_property(proxy, "Alias", &iter)) {
		if (!g_dbus_proxy_get_property(proxy, "Address", &iter))
			goto done;
	}

	path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(&iter, &desc);
	desc = g_strdup_printf(COLOR_BLUE "[%s%s%s]" COLOR_OFF "# ", desc,
				attribute ? ":" : "",
				attribute ? attribute + strlen(path) : "");

done:
	//rl_set_prompt(desc ? desc : PROMPT_ON);
	printf("\r");
	//rl_on_new_line();
	g_free(desc);
}

static void device_added(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	struct adapter *adapter = find_parent(proxy);

	if (!adapter) {
		/* TODO: Error */
		return;
	}

	adapter->devices = g_list_append(adapter->devices, proxy);
	print_device(proxy, COLORED_NEW);

	if (default_dev)
		return;

	if (g_dbus_proxy_get_property(proxy, "Connected", &iter)) {
		dbus_bool_t connected;

		dbus_message_iter_get_basic(&iter, &connected);

		if (connected)
			set_default_device(proxy, NULL);
	}
}

static void adapter_added(GDBusProxy *proxy)
{
	struct adapter *adapter = g_malloc0(sizeof(struct adapter));

	adapter->proxy = proxy;
	ctrl_list = g_list_append(ctrl_list, adapter);

	if (!default_ctrl)
		default_ctrl = adapter;

	print_adapter(proxy, COLORED_NEW);
}

static void proxy_added(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
		device_added(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		adapter_added(proxy);
	} else if (!strcmp(interface, "org.bluez.AgentManager1")) {
		if (!agent_manager) {
			agent_manager = proxy;

			if (auto_register_agent)
				agent_register(dbus_conn, agent_manager,
							auto_register_agent);
		}
	} else if (!strcmp(interface, "org.bluez.GattService1")) {
		if (service_is_child(proxy))
			gatt_add_service(proxy);
	} else if (!strcmp(interface, "org.bluez.GattCharacteristic1")) {
		gatt_add_characteristic(proxy);
	} else if (!strcmp(interface, "org.bluez.GattDescriptor1")) {
		gatt_add_descriptor(proxy);
	} else if (!strcmp(interface, "org.bluez.GattManager1")) {
		gatt_add_manager(proxy);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		ad_manager = proxy;
	}
}

static void set_default_attribute(GDBusProxy *proxy)
{
	const char *path;

	default_attr = proxy;

	path = g_dbus_proxy_get_path(proxy);

	set_default_device(default_dev, path);
}

static void device_removed(GDBusProxy *proxy)
{
	struct adapter *adapter = find_parent(proxy);
	if (!adapter) {
		/* TODO: Error */
		return;
	}

	adapter->devices = g_list_remove(adapter->devices, proxy);

	print_device(proxy, COLORED_DEL);

	if (default_dev == proxy)
		set_default_device(NULL, NULL);
}

static void adapter_removed(GDBusProxy *proxy)
{
	GList *ll;

	for (ll = g_list_first(ctrl_list); ll; ll = g_list_next(ll)) {
		struct adapter *adapter = ll->data;

		if (adapter->proxy == proxy) {
			print_adapter(proxy, COLORED_DEL);

			if (default_ctrl && default_ctrl->proxy == proxy) {
				default_ctrl = NULL;
				set_default_device(NULL, NULL);
			}

			ctrl_list = g_list_remove_link(ctrl_list, ll);
			g_list_free(adapter->devices);
			g_free(adapter);
			g_list_free(ll);
			return;
		}
	}
}

static void proxy_removed(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
		device_removed(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		adapter_removed(proxy);
	} else if (!strcmp(interface, "org.bluez.AgentManager1")) {
		if (agent_manager == proxy) {
			agent_manager = NULL;
			if (auto_register_agent)
				agent_unregister(dbus_conn, NULL);
		}
	} else if (!strcmp(interface, "org.bluez.GattService1")) {
		gatt_remove_service(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);
	} else if (!strcmp(interface, "org.bluez.GattCharacteristic1")) {
		gatt_remove_characteristic(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);
	} else if (!strcmp(interface, "org.bluez.GattDescriptor1")) {
		gatt_remove_descriptor(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);
	} else if (!strcmp(interface, "org.bluez.GattManager1")) {
		gatt_remove_manager(proxy);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		if (ad_manager == proxy) {
			agent_manager = NULL;
			ad_unregister(dbus_conn, NULL);
		}
	}
}

static void property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
		if (default_ctrl && device_is_child(proxy,
					default_ctrl->proxy) == TRUE) {
			DBusMessageIter addr_iter;
			char *str;

			if (g_dbus_proxy_get_property(proxy, "Address",
							&addr_iter) == TRUE) {
				const char *address, *name;

				dbus_message_iter_get_basic(&addr_iter,
								&address);
                                if (g_dbus_proxy_get_property(proxy, "Alias", &addr_iter) == TRUE)
                                        dbus_message_iter_get_basic(&addr_iter, &name);
                                else
                                        name = "<unknown>";
                                //str = g_strdup_printf("[" COLORED_CHG
                                //                      "] Device %s %s", address, name);
                                LOG("[" COLORED_CHG
                                                      "] Device %s %s\n", address, name);
                                str = g_strdup_printf("Device %s", address);

                                 Device *dev = g_slice_new0(Device);
                                 snprintf(dev->name, sizeof(dev->name), "%s", name);
                                 snprintf(dev->address, sizeof(dev->address), "%s", address);
                                 bt_device_chg(async_queue, dev);
			} else
				str = g_strdup("");

			if (strcmp(name, "Connected") == 0) {
				dbus_bool_t connected;

				dbus_message_iter_get_basic(iter, &connected);

				if (connected && default_dev == NULL)
					set_default_device(proxy, NULL);
				else if (!connected && default_dev == proxy)
					set_default_device(NULL, NULL);
			}

			print_iter(str, name, iter);
			g_free(str);
		}
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		DBusMessageIter addr_iter;
		char *str;

		if (g_dbus_proxy_get_property(proxy, "Address",
						&addr_iter) == TRUE) {
			const char *address;

			dbus_message_iter_get_basic(&addr_iter, &address);
			str = g_strdup_printf("[" COLORED_CHG
						"] Controller %s ", address);
		} else
			str = g_strdup("");

		print_iter(str, name, iter);
		g_free(str);
	} else if (proxy == default_attr) {
		char *str;

		str = g_strdup_printf("[" COLORED_CHG "] Attribute %s ",
						g_dbus_proxy_get_path(proxy));

		print_iter(str, name, iter);
		g_free(str);
	}
}

static void message_handler(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	LOG("[SIGNAL] %s.%s\n", dbus_message_get_interface(message),
					dbus_message_get_member(message));
}

static struct adapter *find_ctrl_by_address(GList *source, const char *address)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		struct adapter *adapter = list->data;
		DBusMessageIter iter;
		const char *str;

		if (g_dbus_proxy_get_property(adapter->proxy,
					"Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strcmp(str, address))
			return adapter;
	}

	return NULL;
}

static GDBusProxy *find_proxy_by_address(GList *source, const char *address)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		GDBusProxy *proxy = list->data;
		DBusMessageIter iter;
		const char *str;

		if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strcmp(str, address))
			return proxy;
	}

	return NULL;
}

static gboolean check_default_ctrl(void)
{
	if (!default_ctrl) {
		LOG("No default controller available\n");
		return FALSE;
	}

	return TRUE;
}

static gboolean parse_argument_on_off(const char *arg, dbus_bool_t *value)
{
	if (!arg || !strlen(arg)) {
		LOG("Missing on/off argument\n");
		return FALSE;
	}

	if (!strcmp(arg, "on") || !strcmp(arg, "yes")) {
		*value = TRUE;
		return TRUE;
	}

	if (!strcmp(arg, "off") || !strcmp(arg, "no")) {
		*value = FALSE;
		return TRUE;
	}

	LOG("Invalid argument %s\n", arg);
	return FALSE;
}

static gboolean parse_argument_agent(const char *arg, dbus_bool_t *value,
							const char **capability)
{
	const char * const *opt;

	if (arg == NULL || strlen(arg) == 0) {
		LOG("Missing on/off/capability argument\n");
		return FALSE;
	}

	if (strcmp(arg, "on") == 0 || strcmp(arg, "yes") == 0) {
		*value = TRUE;
		*capability = "";
		return TRUE;
	}

	if (strcmp(arg, "off") == 0 || strcmp(arg, "no") == 0) {
		*value = FALSE;
		return TRUE;
	}

	for (opt = agent_arguments; *opt; opt++) {
		if (strcmp(arg, *opt) == 0) {
			*value = TRUE;
			*capability = *opt;
			return TRUE;
		}
	}

	LOG("Invalid argument %s\n", arg);
	return FALSE;
}

static void cmd_list(const char *arg)
{
	GList *list;

	for (list = g_list_first(ctrl_list); list; list = g_list_next(list)) {
		struct adapter *adapter = list->data;
		print_adapter(adapter->proxy, NULL);
	}
}

static void cmd_show(const char *arg)
{
	struct adapter *adapter;
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *address;

	if (!arg || !strlen(arg)) {
		if (check_default_ctrl() == FALSE)
			return;

		proxy = default_ctrl->proxy;
	} else {
		adapter = find_ctrl_by_address(ctrl_list, arg);
		if (!adapter) {
			LOG("Controller %s not available\n", arg);
			return;
		}
		proxy = adapter->proxy;
	}

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);
	LOG("Controller %s\n", address);

	print_property(proxy, "Name");
	print_property(proxy, "Alias");
	print_property(proxy, "Class");
	print_property(proxy, "Powered");
	print_property(proxy, "Discoverable");
	print_property(proxy, "Pairable");
	print_uuids(proxy);
	print_property(proxy, "Modalias");
	print_property(proxy, "Discovering");
}

static void cmd_select(const char *arg)
{
	struct adapter *adapter;

	if (!arg || !strlen(arg)) {
		LOG("Missing controller address argument\n");
		return;
	}

	adapter = find_ctrl_by_address(ctrl_list, arg);
	if (!adapter) {
		LOG("Controller %s not available\n", arg);
		return;
	}

	if (default_ctrl && default_ctrl->proxy == adapter->proxy)
		return;

	default_ctrl = adapter;
	print_adapter(adapter->proxy, NULL);
}

static void cmd_devices(const char *arg)
{
	GList *ll;

	if (check_default_ctrl() == FALSE)
		return;

	for (ll = g_list_first(default_ctrl->devices);
			ll; ll = g_list_next(ll)) {
		GDBusProxy *proxy = ll->data;
		print_device(proxy, NULL);
	}
}

static void cmd_paired_devices(const char *arg)
{
	GList *ll;

	if (check_default_ctrl() == FALSE)
		return;

	for (ll = g_list_first(default_ctrl->devices);
			ll; ll = g_list_next(ll)) {
		GDBusProxy *proxy = ll->data;
		DBusMessageIter iter;
		dbus_bool_t paired;

		if (g_dbus_proxy_get_property(proxy, "Paired", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &paired);
		if (!paired)
			continue;

		print_device(proxy, NULL);
	}
}

static void generic_callback(const DBusError *error, void *user_data)
{
	char *str = user_data;

	if (dbus_error_is_set(error))
		LOG("Failed to set %s: %s\n", str, error->name);
	else
		LOG("Changing %s succeeded\n", str);
}

static void cmd_system_alias(const char *arg)
{
	char *name;

	if (!arg || !strlen(arg)) {
		LOG("Missing name argument\n");
		return;
	}

	if (check_default_ctrl() == FALSE)
		return;

	name = g_strdup(arg);

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Alias",
					DBUS_TYPE_STRING, &name,
					generic_callback, name, g_free) == TRUE)
		return;

	g_free(name);
}

static void cmd_reset_alias(const char *arg)
{
	char *name;

	if (check_default_ctrl() == FALSE)
		return;

	name = g_strdup("");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Alias",
					DBUS_TYPE_STRING, &name,
					generic_callback, name, g_free) == TRUE)
		return;

	g_free(name);
}

static void cmd_power(const char *arg)
{
	dbus_bool_t powered;
	char *str;

	if (parse_argument_on_off(arg, &powered) == FALSE) {
                LOG("power %s fail ========\n", arg);
		return;
        }

	if (check_default_ctrl() == FALSE) {
                LOG("power %s fail++++++++++\n", arg);
		return;
        }

	str = g_strdup_printf("power %s", powered == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Powered",
					DBUS_TYPE_BOOLEAN, &powered,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void cmd_pairable(const char *arg)
{
	dbus_bool_t pairable;
	char *str;

	if (parse_argument_on_off(arg, &pairable) == FALSE)
		return;

	if (check_default_ctrl() == FALSE)
		return;

	str = g_strdup_printf("pairable %s", pairable == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Pairable",
					DBUS_TYPE_BOOLEAN, &pairable,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void cmd_discoverable(const char *arg)
{
	dbus_bool_t discoverable;
	char *str;

	if (parse_argument_on_off(arg, &discoverable) == FALSE)
		return;

	if (check_default_ctrl() == FALSE)
		return;

	str = g_strdup_printf("discoverable %s",
				discoverable == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Discoverable",
					DBUS_TYPE_BOOLEAN, &discoverable,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void cmd_agent(const char *arg)
{
	dbus_bool_t enable;
	const char *capability;

	if (parse_argument_agent(arg, &enable, &capability) == FALSE)
		return;

	if (enable == TRUE) {
		g_free(auto_register_agent);
		auto_register_agent = g_strdup(capability);

		if (agent_manager)
			agent_register(dbus_conn, agent_manager,
						auto_register_agent);
		else
			LOG("Agent registration enabled\n");
	} else {
		g_free(auto_register_agent);
		auto_register_agent = NULL;

		if (agent_manager)
			agent_unregister(dbus_conn, agent_manager);
		else
			LOG("Agent registration disabled\n");
	}
}

static void cmd_default_agent(const char *arg)
{
	agent_default(dbus_conn, agent_manager);
}

static void start_discovery_reply(DBusMessage *message, void *user_data)
{
	dbus_bool_t enable = GPOINTER_TO_UINT(user_data);
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to %s discovery: %s\n",
				enable == TRUE ? "start" : "stop", error.name);
		dbus_error_free(&error);
		return;
	}

	LOG("Discovery %s\n", enable == TRUE ? "started" : "stopped");
}

static void cmd_scan(const char *arg)
{
	dbus_bool_t enable;
	const char *method;

	if (parse_argument_on_off(arg, &enable) == FALSE)
		return;

	if (check_default_ctrl() == FALSE)
		return;

	if (enable == TRUE)
		method = "StartDiscovery";
	else
		method = "StopDiscovery";

	if (g_dbus_proxy_method_call(default_ctrl->proxy, method,
				NULL, start_discovery_reply,
				GUINT_TO_POINTER(enable), NULL) == FALSE) {
		LOG("Failed to %s discovery\n",
					enable == TRUE ? "start" : "stop");
		return;
	}
}

static void append_variant(DBusMessageIter *iter, int type, void *val)
{
	DBusMessageIter value;
	char sig[2] = { type, '\0' };

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, sig, &value);

	dbus_message_iter_append_basic(&value, type, val);

	dbus_message_iter_close_container(iter, &value);
}

static void append_array_variant(DBusMessageIter *iter, int type, void *val,
							int n_elements)
{
	DBusMessageIter variant, array;
	char type_sig[2] = { type, '\0' };
	char array_sig[3] = { DBUS_TYPE_ARRAY, type, '\0' };

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
						array_sig, &variant);

	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
						type_sig, &array);

	if (dbus_type_is_fixed(type) == TRUE) {
		dbus_message_iter_append_fixed_array(&array, type, val,
							n_elements);
	} else if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH) {
		const char ***str_array = val;
		int i;

		for (i = 0; i < n_elements; i++)
			dbus_message_iter_append_basic(&array, type,
							&((*str_array)[i]));
	}

	dbus_message_iter_close_container(&variant, &array);

	dbus_message_iter_close_container(iter, &variant);
}

static void dict_append_entry(DBusMessageIter *dict, const char *key,
							int type, void *val)
{
	DBusMessageIter entry;

	if (type == DBUS_TYPE_STRING) {
		const char *str = *((const char **) val);

		if (str == NULL)
			return;
	}

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
							NULL, &entry);

	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);

	append_variant(&entry, type, val);

	dbus_message_iter_close_container(dict, &entry);
}

static void dict_append_basic_array(DBusMessageIter *dict, int key_type,
					const void *key, int type, void *val,
					int n_elements)
{
	DBusMessageIter entry;

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY,
						NULL, &entry);

	dbus_message_iter_append_basic(&entry, key_type, key);

	append_array_variant(&entry, type, val, n_elements);

	dbus_message_iter_close_container(dict, &entry);
}

static void dict_append_array(DBusMessageIter *dict, const char *key, int type,
						void *val, int n_elements)
{
	dict_append_basic_array(dict, DBUS_TYPE_STRING, &key, type, val,
								n_elements);
}

#define	DISTANCE_VAL_INVALID	0x7FFF

struct set_discovery_filter_args {
	char *transport;
	dbus_uint16_t rssi;
	dbus_int16_t pathloss;
	char **uuids;
	size_t uuids_len;
};

static void set_discovery_filter_setup(DBusMessageIter *iter, void *user_data)
{
	struct set_discovery_filter_args *args = user_data;
	DBusMessageIter dict;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	dict_append_array(&dict, "UUIDs", DBUS_TYPE_STRING, &args->uuids,
							args->uuids_len);

	if (args->pathloss != DISTANCE_VAL_INVALID)
		dict_append_entry(&dict, "Pathloss", DBUS_TYPE_UINT16,
						&args->pathloss);

	if (args->rssi != DISTANCE_VAL_INVALID)
		dict_append_entry(&dict, "RSSI", DBUS_TYPE_INT16, &args->rssi);

	if (args->transport != NULL)
		dict_append_entry(&dict, "Transport", DBUS_TYPE_STRING,
						&args->transport);

	dbus_message_iter_close_container(iter, &dict);
}


static void set_discovery_filter_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("SetDiscoveryFilter failed: %s\n", error.name);
		dbus_error_free(&error);
		return;
	}

	LOG("SetDiscoveryFilter success\n");
}

static gint filtered_scan_rssi = DISTANCE_VAL_INVALID;
static gint filtered_scan_pathloss = DISTANCE_VAL_INVALID;
static char **filtered_scan_uuids;
static size_t filtered_scan_uuids_len;
static char *filtered_scan_transport;

static void cmd_set_scan_filter_commit(void)
{
	struct set_discovery_filter_args args;

	args.uuids = NULL;
	args.pathloss = filtered_scan_pathloss;
	args.rssi = filtered_scan_rssi;
	args.transport = filtered_scan_transport;
	args.uuids = filtered_scan_uuids;
	args.uuids_len = filtered_scan_uuids_len;

	if (check_default_ctrl() == FALSE)
		return;

	if (g_dbus_proxy_method_call(default_ctrl->proxy, "SetDiscoveryFilter",
		set_discovery_filter_setup, set_discovery_filter_reply,
		&args, NULL) == FALSE) {
		LOG("Failed to set discovery filter\n");
		return;
	}
}

static void cmd_set_scan_filter_uuids(const char *arg)
{
	g_strfreev(filtered_scan_uuids);
	filtered_scan_uuids = NULL;
	filtered_scan_uuids_len = 0;

	if (!arg || !strlen(arg))
		goto commit;

	filtered_scan_uuids = g_strsplit(arg, " ", -1);
	if (!filtered_scan_uuids) {
		LOG("Failed to parse input\n");
		return;
	}

	filtered_scan_uuids_len = g_strv_length(filtered_scan_uuids);

commit:
	cmd_set_scan_filter_commit();
}

static void cmd_set_scan_filter_rssi(const char *arg)
{
	filtered_scan_pathloss = DISTANCE_VAL_INVALID;

	if (!arg || !strlen(arg))
		filtered_scan_rssi = DISTANCE_VAL_INVALID;
	else
		filtered_scan_rssi = atoi(arg);

	cmd_set_scan_filter_commit();
}

static void cmd_set_scan_filter_pathloss(const char *arg)
{
	filtered_scan_rssi = DISTANCE_VAL_INVALID;

	if (!arg || !strlen(arg))
		filtered_scan_pathloss = DISTANCE_VAL_INVALID;
	else
		filtered_scan_pathloss = atoi(arg);

	cmd_set_scan_filter_commit();
}

static void cmd_set_scan_filter_transport(const char *arg)
{
	g_free(filtered_scan_transport);

	if (!arg || !strlen(arg))
		filtered_scan_transport = NULL;
	else
		filtered_scan_transport = g_strdup(arg);

	cmd_set_scan_filter_commit();
}

static void clear_discovery_filter_setup(DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter dict;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	dbus_message_iter_close_container(iter, &dict);
}

static void cmd_set_scan_filter_clear(const char *arg)
{
	/* set default values for all options */
	filtered_scan_rssi = DISTANCE_VAL_INVALID;
	filtered_scan_pathloss = DISTANCE_VAL_INVALID;
	g_strfreev(filtered_scan_uuids);
	filtered_scan_uuids = NULL;
	filtered_scan_uuids_len = 0;
	g_free(filtered_scan_transport);
	filtered_scan_transport = NULL;

	if (check_default_ctrl() == FALSE)
		return;

	if (g_dbus_proxy_method_call(default_ctrl->proxy, "SetDiscoveryFilter",
		clear_discovery_filter_setup, set_discovery_filter_reply,
		NULL, NULL) == FALSE) {
		LOG("Failed to clear discovery filter\n");
	}
}

static struct GDBusProxy *find_device(const char *arg)
{
	GDBusProxy *proxy;

	if (!arg || !strlen(arg)) {
		if (default_dev)
			return default_dev;
		LOG("Missing device address argument\n");
		return NULL;
	}

	if (check_default_ctrl() == FALSE)
		return NULL;

	proxy = find_proxy_by_address(default_ctrl->devices, arg);
	if (!proxy) {
		LOG("Device %s not available\n", arg);
		return NULL;
	}

	return proxy;
}

static void cmd_info(const char *arg)
{
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *address;

	proxy = find_device(arg);
	if (!proxy)
		return;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);
	LOG("Device %s\n", address);

	print_property(proxy, "Name");
	print_property(proxy, "Alias");
	print_property(proxy, "Class");
	print_property(proxy, "Appearance");
	print_property(proxy, "Icon");
	print_property(proxy, "Paired");
	print_property(proxy, "Trusted");
	print_property(proxy, "Blocked");
	print_property(proxy, "Connected");
	print_property(proxy, "LegacyPairing");
	print_uuids(proxy);
	print_property(proxy, "Modalias");
	print_property(proxy, "ManufacturerData");
	print_property(proxy, "ServiceData");
	print_property(proxy, "RSSI");
	print_property(proxy, "TxPower");
}

static void pair_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to pair: %s\n", error.name);
		dbus_error_free(&error);
		return;
	}

	LOG("Pairing successful\n");
}

static void cmd_pair(const char *arg)
{
	GDBusProxy *proxy;

	proxy = find_device(arg);
	if (!proxy)
		return;

	if (g_dbus_proxy_method_call(proxy, "Pair", NULL, pair_reply,
							NULL, NULL) == FALSE) {
		LOG("Failed to pair\n");
		return;
	}

	LOG("Attempting to pair with %s\n", arg);
}

static void cmd_trust(const char *arg)
{
	GDBusProxy *proxy;
	dbus_bool_t trusted;
	char *str;

	proxy = find_device(arg);
	if (!proxy)
		return;

	trusted = TRUE;

	str = g_strdup_printf("%s trust", arg);

	if (g_dbus_proxy_set_property_basic(proxy, "Trusted",
					DBUS_TYPE_BOOLEAN, &trusted,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void cmd_untrust(const char *arg)
{
	GDBusProxy *proxy;
	dbus_bool_t trusted;
	char *str;

	proxy = find_device(arg);
	if (!proxy)
		return;

	trusted = FALSE;

	str = g_strdup_printf("%s untrust", arg);

	if (g_dbus_proxy_set_property_basic(proxy, "Trusted",
					DBUS_TYPE_BOOLEAN, &trusted,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void cmd_block(const char *arg)
{
	GDBusProxy *proxy;
	dbus_bool_t blocked;
	char *str;

	proxy = find_device(arg);
	if (!proxy)
		return;

	blocked = TRUE;

	str = g_strdup_printf("%s block", arg);

	if (g_dbus_proxy_set_property_basic(proxy, "Blocked",
					DBUS_TYPE_BOOLEAN, &blocked,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void cmd_unblock(const char *arg)
{
	GDBusProxy *proxy;
	dbus_bool_t blocked;
	char *str;

	proxy = find_device(arg);
	if (!proxy)
		return;

	blocked = FALSE;

	str = g_strdup_printf("%s unblock", arg);

	if (g_dbus_proxy_set_property_basic(proxy, "Blocked",
					DBUS_TYPE_BOOLEAN, &blocked,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void remove_device_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to remove device: %s\n", error.name);
		dbus_error_free(&error);
		return;
	}

	LOG("Device has been removed\n");
}

static void remove_device_setup(DBusMessageIter *iter, void *user_data)
{
	const char *path = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

static void remove_device(GDBusProxy *proxy)
{
	char *path;

	path = g_strdup(g_dbus_proxy_get_path(proxy));

	if (!default_ctrl)
		return;

	if (g_dbus_proxy_method_call(default_ctrl->proxy, "RemoveDevice",
						remove_device_setup,
						remove_device_reply,
						path, g_free) == FALSE) {
		LOG("Failed to remove device\n");
		g_free(path);
	}
}

static void cmd_remove(const char *arg)
{
	GDBusProxy *proxy;

	if (!arg || !strlen(arg)) {
		LOG("Missing device address argument\n");
		return;
	}

	if (check_default_ctrl() == FALSE)
		return;

	if (strcmp(arg, "*") == 0) {
		GList *list;

		for (list = default_ctrl->devices; list;
						list = g_list_next(list)) {
			GDBusProxy *proxy = list->data;

			remove_device(proxy);
		}
		return;
	}

	proxy = find_proxy_by_address(default_ctrl->devices, arg);
	if (!proxy) {
		LOG("Device %s not available\n", arg);
		return;
	}

	remove_device(proxy);
}

static void connect_reply(DBusMessage *message, void *user_data)
{
	GDBusProxy *proxy = user_data;
	DBusError error;

	dbus_error_init(&error);

	DBusMessageIter iter;
	const char *address, *name;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to connect: %s [%s %s]\n", error.name, address, name);
		dbus_error_free(&error);

                Device *dev = g_slice_new0(Device);
                snprintf(dev->name, sizeof(dev->name), "%s", name);
                snprintf(dev->address, sizeof(dev->address), "%s", address);
                dev->connected = 0;
                bt_device_disconn(async_queue, dev);
                return;
	}

	LOG("%s%s%sDevice %s %s Connection successful\n",
				NULL ? "[" : "",
				NULL ? : "",
				NULL ? "] " : "",
				address, name);

	//LOG("Connection successful\n");
        // FIXME
        Device *dev = g_slice_new0(Device);
        snprintf(dev->name, sizeof(dev->name), "%s", name);
        snprintf(dev->address, sizeof(dev->address), "%s", address);
        dev->connected = 1;
        bt_device_conn(async_queue, dev);

	set_default_device(proxy, NULL);
}

static void cmd_connect(const char *arg)
{
	GDBusProxy *proxy;

	if (!arg || !strlen(arg)) {
		LOG("Missing device address argument\n");
		return;
	}

	if (check_default_ctrl() == FALSE)
		return;

	proxy = find_proxy_by_address(default_ctrl->devices, arg);
	if (!proxy) {
		LOG("Device %s not available\n", arg);
		return;
	}

	if (g_dbus_proxy_method_call(proxy, "Connect", NULL, connect_reply,
							proxy, NULL) == FALSE) {
		LOG("Failed to connect\n");
		return;
	}

	LOG("Attempting to connect to %s\n", arg);
}

static void disconn_reply(DBusMessage *message, void *user_data)
{
	GDBusProxy *proxy = user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to disconnect: %s\n", error.name);
		dbus_error_free(&error);
		return;
	}

	LOG("Successful disconnected\n");

	if (proxy != default_dev)
		return;

	set_default_device(NULL, NULL);
}

static void cmd_disconn(const char *arg)
{
	GDBusProxy *proxy;

	proxy = find_device(arg);
	if (!proxy)
		return;

	if (g_dbus_proxy_method_call(proxy, "Disconnect", NULL, disconn_reply,
							proxy, NULL) == FALSE) {
		LOG("Failed to disconnect\n");
		return;
	}
	if (strlen(arg) == 0) {
		DBusMessageIter iter;

		if (g_dbus_proxy_get_property(proxy, "Address", &iter) == TRUE)
			dbus_message_iter_get_basic(&iter, &arg);
	}
	LOG("Attempting to disconnect from %s\n", arg);
}

static void cmd_list_attributes(const char *arg)
{
	GDBusProxy *proxy;

	proxy = find_device(arg);
	if (!proxy)
		return;

	gatt_list_attributes(g_dbus_proxy_get_path(proxy));
}

static void cmd_set_alias(const char *arg)
{
	char *name;

	if (!arg || !strlen(arg)) {
		LOG("Missing name argument\n");
		return;
	}

	if (!default_dev) {
		LOG("No device connected\n");
		return;
	}

	name = g_strdup(arg);

	if (g_dbus_proxy_set_property_basic(default_dev, "Alias",
					DBUS_TYPE_STRING, &name,
					generic_callback, name, g_free) == TRUE)
		return;

	g_free(name);
}

static void cmd_select_attribute(const char *arg)
{
	GDBusProxy *proxy;

	if (!arg || !strlen(arg)) {
		LOG("Missing attribute argument\n");
		return;
	}

	if (!default_dev) {
		LOG("No device connected\n");
		return;
	}

	proxy = gatt_select_attribute(arg);
	if (proxy)
		set_default_attribute(proxy);
}

static struct GDBusProxy *find_attribute(const char *arg)
{
	GDBusProxy *proxy;

	if (!arg || !strlen(arg)) {
		if (default_attr)
			return default_attr;
		LOG("Missing attribute argument\n");
		return NULL;
	}

	proxy = gatt_select_attribute(arg);
	if (!proxy) {
		LOG("Attribute %s not available\n", arg);
		return NULL;
	}

	return proxy;
}

static void cmd_attribute_info(const char *arg)
{
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *iface, *uuid, *text;

	proxy = find_attribute(arg);
	if (!proxy)
		return;

	if (g_dbus_proxy_get_property(proxy, "UUID", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &uuid);

	text = uuidstr_to_str(uuid);
	if (!text)
		text = g_dbus_proxy_get_path(proxy);

	iface = g_dbus_proxy_get_interface(proxy);
	if (!strcmp(iface, "org.bluez.GattService1")) {
		LOG("Service - %s\n", text);

		print_property(proxy, "UUID");
		print_property(proxy, "Primary");
		print_property(proxy, "Characteristics");
		print_property(proxy, "Includes");
	} else if (!strcmp(iface, "org.bluez.GattCharacteristic1")) {
		LOG("Characteristic - %s\n", text);

		print_property(proxy, "UUID");
		print_property(proxy, "Service");
		print_property(proxy, "Value");
		print_property(proxy, "Notifying");
		print_property(proxy, "Flags");
		print_property(proxy, "Descriptors");
	} else if (!strcmp(iface, "org.bluez.GattDescriptor1")) {
		LOG("Descriptor - %s\n", text);

		print_property(proxy, "UUID");
		print_property(proxy, "Characteristic");
		print_property(proxy, "Value");
	}
}

static void cmd_read(const char *arg)
{
	if (!default_attr) {
		LOG("No attribute selected\n");
		return;
	}

	gatt_read_attribute(default_attr);
}

static void cmd_write(const char *arg)
{
	if (!arg || !strlen(arg)) {
		LOG("Missing data argument\n");
		return;
	}

	if (!default_attr) {
		LOG("No attribute selected\n");
		return;
	}

	gatt_write_attribute(default_attr, arg);
}

static void cmd_notify(const char *arg)
{
	dbus_bool_t enable;

	if (parse_argument_on_off(arg, &enable) == FALSE)
		return;

	if (!default_attr) {
		LOG("No attribute selected\n");
		return;
	}

	gatt_notify_attribute(default_attr, enable ? true : false);
}

static void cmd_register_profile(const char *arg)
{
	wordexp_t w;

	if (check_default_ctrl() == FALSE)
		return;

	if (wordexp(arg, &w, WRDE_NOCMD)) {
		LOG("Invalid argument\n");
		return;
	}

	if (w.we_wordc == 0) {
		LOG("Missing argument\n");
		return;
	}

	gatt_register_profile(dbus_conn, default_ctrl->proxy, &w);

	wordfree(&w);
}

static void cmd_unregister_profile(const char *arg)
{
	if (check_default_ctrl() == FALSE)
		return;

	gatt_unregister_profile(dbus_conn, default_ctrl->proxy);
}

static void cmd_version(const char *arg)
{
	LOG("Version %s\n", VERSION);
}

static void cmd_quit(const char *arg)
{
	g_main_loop_quit(main_loop);
}

static char *generic_generator(const char *text, int state,
					GList *source, const char *property)
{
	static int index, len;
	GList *list;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	for (list = g_list_nth(source, index); list;
						list = g_list_next(list)) {
		GDBusProxy *proxy = list->data;
		DBusMessageIter iter;
		const char *str;

		index++;

		if (g_dbus_proxy_get_property(proxy, property, &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strncmp(str, text, len))
			return strdup(str);
        }

	return NULL;
}

static char *ctrl_generator(const char *text, int state)
{
	static int index = 0;
	static int len = 0;
	GList *list;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	for (list = g_list_nth(ctrl_list, index); list;
						list = g_list_next(list)) {
		struct adapter *adapter = list->data;
		DBusMessageIter iter;
		const char *str;

		index++;

		if (g_dbus_proxy_get_property(adapter->proxy,
					"Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strncmp(str, text, len))
			return strdup(str);
	}

	return NULL;
}

static char *dev_generator(const char *text, int state)
{
	return generic_generator(text, state,
			default_ctrl ? default_ctrl->devices : NULL, "Address");
}

static char *attribute_generator(const char *text, int state)
{
	return gatt_attribute_generator(text, state);
}

static char *capability_generator(const char *text, int state)
{
	static int index, len;
	const char *arg;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	while ((arg = agent_arguments[index])) {
		index++;

		if (!strncmp(arg, text, len))
			return strdup(arg);
	}

	return NULL;
}

static gboolean parse_argument_advertise(const char *arg, dbus_bool_t *value,
							const char **type)
{
	const char * const *opt;

	if (arg == NULL || strlen(arg) == 0) {
		LOG("Missing on/off/type argument\n");
		return FALSE;
	}

	if (strcmp(arg, "on") == 0 || strcmp(arg, "yes") == 0) {
		*value = TRUE;
		*type = "";
		return TRUE;
	}

	if (strcmp(arg, "off") == 0 || strcmp(arg, "no") == 0) {
		*value = FALSE;
		return TRUE;
	}

	for (opt = ad_arguments; *opt; opt++) {
		if (strcmp(arg, *opt) == 0) {
			*value = TRUE;
			*type = *opt;
			return TRUE;
		}
	}

	LOG("Invalid argument %s\n", arg);
	return FALSE;
}

static void cmd_advertise(const char *arg)
{
	dbus_bool_t enable;
	const char *type;

	if (parse_argument_advertise(arg, &enable, &type) == FALSE)
		return;

	if (!ad_manager) {
		LOG("LEAdvertisingManager not found\n");
		return;
	}

	if (enable == TRUE)
		ad_register(dbus_conn, ad_manager, type);
	else
		ad_unregister(dbus_conn, ad_manager);
}

static char *ad_generator(const char *text, int state)
{
	static int index, len;
	const char *arg;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	while ((arg = ad_arguments[index])) {
		index++;

		if (!strncmp(arg, text, len))
			return strdup(arg);
	}

	return NULL;
}

static void cmd_set_advertise_uuids(const char *arg)
{
	ad_advertise_uuids(arg);
}

static void cmd_set_advertise_service(const char *arg)
{
	ad_advertise_service(arg);
}

static void cmd_set_advertise_manufacturer(const char *arg)
{
	ad_advertise_manufacturer(arg);
}

static void cmd_set_advertise_tx_power(const char *arg)
{
	if (arg == NULL || strlen(arg) == 0) {
		LOG("Missing on/off argument\n");
		return;
	}

	if (strcmp(arg, "on") == 0 || strcmp(arg, "yes") == 0) {
		ad_advertise_tx_power(TRUE);
		return;
	}

	if (strcmp(arg, "off") == 0 || strcmp(arg, "no") == 0) {
		ad_advertise_tx_power(FALSE);
		return;
	}

	LOG("Invalid argument\n");
}

static const struct {
	const char *cmd;
	const char *arg;
	void (*func) (const char *arg);
	const char *desc;
	char * (*gen) (const char *text, int state);
	void (*disp) (char **matches, int num_matches, int max_length);
} cmd_table[] = {
	{ "list",         NULL,       cmd_list, "List available controllers" },
	{ "show",         "[ctrl]",   cmd_show, "Controller information",
							ctrl_generator },
	{ "select",       "<ctrl>",   cmd_select, "Select default controller",
							ctrl_generator },
	{ "devices",      NULL,       cmd_devices, "List available devices" },
	{ "paired-devices", NULL,     cmd_paired_devices,
					"List paired devices"},
	{ "system-alias", "<name>",   cmd_system_alias },
	{ "reset-alias",  NULL,       cmd_reset_alias },
	{ "power",        "<on/off>", cmd_power, "Set controller power" },
	{ "pairable",     "<on/off>", cmd_pairable,
					"Set controller pairable mode" },
	{ "discoverable", "<on/off>", cmd_discoverable,
					"Set controller discoverable mode" },
	{ "agent",        "<on/off/capability>", cmd_agent,
				"Enable/disable agent with given capability",
							capability_generator},
	{ "default-agent",NULL,       cmd_default_agent,
				"Set agent as the default one" },
	{ "advertise",    "<on/off/type>", cmd_advertise,
				"Enable/disable advertising with given type",
							ad_generator},
	{ "set-advertise-uuids", "[uuid1 uuid2 ...]",
			cmd_set_advertise_uuids, "Set advertise uuids" },
	{ "set-advertise-service", "[uuid][data=[xx xx ...]",
			cmd_set_advertise_service,
			"Set advertise service data" },
	{ "set-advertise-manufacturer", "[id][data=[xx xx ...]",
			cmd_set_advertise_manufacturer,
			"Set advertise manufacturer data" },
	{ "set-advertise-tx-power", "<on/off>",
			cmd_set_advertise_tx_power,
			"Enable/disable TX power to be advertised" },
	{ "set-scan-filter-uuids", "[uuid1 uuid2 ...]",
			cmd_set_scan_filter_uuids, "Set scan filter uuids" },
	{ "set-scan-filter-rssi", "[rssi]", cmd_set_scan_filter_rssi,
				"Set scan filter rssi, and clears pathloss" },
	{ "set-scan-filter-pathloss", "[pathloss]",
						cmd_set_scan_filter_pathloss,
				"Set scan filter pathloss, and clears rssi" },
	{ "set-scan-filter-transport", "[transport]",
		cmd_set_scan_filter_transport, "Set scan filter transport" },
	{ "set-scan-filter-clear", "", cmd_set_scan_filter_clear,
						"Clears discovery filter." },
	{ "scan",         "<on/off>", cmd_scan, "Scan for devices" },
	{ "info",         "[dev]",    cmd_info, "Device information",
							dev_generator },
	{ "pair",         "[dev]",    cmd_pair, "Pair with device",
							dev_generator },
	{ "trust",        "[dev]",    cmd_trust, "Trust device",
							dev_generator },
	{ "untrust",      "[dev]",    cmd_untrust, "Untrust device",
							dev_generator },
	{ "block",        "[dev]",    cmd_block, "Block device",
								dev_generator },
	{ "unblock",      "[dev]",    cmd_unblock, "Unblock device",
								dev_generator },
	{ "remove",       "<dev>",    cmd_remove, "Remove device",
							dev_generator },
	{ "connect",      "<dev>",    cmd_connect, "Connect device",
							dev_generator },
	{ "disconnect",   "[dev]",    cmd_disconn, "Disconnect device",
							dev_generator },
	{ "list-attributes", "[dev]", cmd_list_attributes, "List attributes",
							dev_generator },
	{ "set-alias",    "<alias>",  cmd_set_alias, "Set device alias" },
	{ "select-attribute", "<attribute>",  cmd_select_attribute,
				"Select attribute", attribute_generator },
	{ "attribute-info", "[attribute]",  cmd_attribute_info,
				"Select attribute", attribute_generator },
	{ "read",         NULL,       cmd_read, "Read attribute value" },
	{ "write",        "<data=[xx xx ...]>", cmd_write,
						"Write attribute value" },
	{ "notify",       "<on/off>", cmd_notify, "Notify attribute value" },
	{ "register-profile", "<UUID ...>", cmd_register_profile,
						"Register profile to connect" },
	{ "unregister-profile", NULL, cmd_unregister_profile,
						"Unregister profile" },
	{ "version",      NULL,       cmd_version, "Display version" },
	{ "quit",         NULL,       cmd_quit, "Quit program" },
	{ "exit",         NULL,       cmd_quit },
	{ "help" },
	{ }
};

static char *cmd_generator(const char *text, int state)
{
	static int index, len;
	const char *cmd;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	while ((cmd = cmd_table[index].cmd)) {
		index++;

		if (!strncmp(cmd, text, len))
			return strdup(cmd);
	}

	return NULL;
}

#if 0
static char **cmd_completion(const char *text, int start, int end)
{
	char **matches = NULL;

	if (agent_completion() == TRUE) {
		rl_attempted_completion_over = 1;
		return NULL;
	}

	if (start > 0) {
		int i;

		for (i = 0; cmd_table[i].cmd; i++) {
			if (strncmp(cmd_table[i].cmd,
					rl_line_buffer, start - 1))
				continue;

			if (!cmd_table[i].gen)
				continue;

			rl_completion_display_matches_hook = cmd_table[i].disp;
			matches = rl_completion_matches(text, cmd_table[i].gen);
			break;
		}
	} else {
		rl_completion_display_matches_hook = NULL;
		matches = rl_completion_matches(text, cmd_generator);
	}

	if (!matches)
		rl_attempted_completion_over = 1;

	return matches;
}
#endif

#if 0
static void rl_handler(char *input)
{
	char *cmd, *arg;
	int i;

	if (!input) {
		rl_insert_text("quit");
		rl_redisplay();
		rl_crlf();
		g_main_loop_quit(main_loop);
		return;
	}

	if (!strlen(input))
		goto done;

	if (agent_input(dbus_conn, input) == TRUE)
		goto done;

	add_history(input);

	cmd = strtok_r(input, " ", &arg);
	if (!cmd)
		goto done;

	if (arg) {
		int len = strlen(arg);
		if (len > 0 && arg[len - 1] == ' ')
			arg[len - 1] = '\0';
	}

	for (i = 0; cmd_table[i].cmd; i++) {
		if (strcmp(cmd, cmd_table[i].cmd))
			continue;

		if (cmd_table[i].func) {
			cmd_table[i].func(arg);
			goto done;
		}
	}

	if (strcmp(cmd, "help")) {
		printf("Invalid command\n");
		goto done;
	}

	printf("Available commands:\n");

	for (i = 0; cmd_table[i].cmd; i++) {
		if (cmd_table[i].desc)
			printf("  %s %-*s %s\n", cmd_table[i].cmd,
					(int)(25 - strlen(cmd_table[i].cmd)),
					cmd_table[i].arg ? : "",
					cmd_table[i].desc ? : "");
	}

done:
	free(input);
}
#endif

static gpointer cmd_handle(gpointer data)
{
        GAsyncQueue *cmd_queue = data;
        CMD *event = NULL;
        int len = 0;


        while (event = g_async_queue_pop (cmd_queue)) {
#if 0
        for (;;) {
        retry:
                len = g_async_queue_length (cmd_queue);
                if (len <= 0) {
                        //LOG("recvfrom error, will retry \n");
                        usleep(500000);
                        goto retry;
                }
                event = g_async_queue_pop (cmd_queue);
#endif
            	char *cmd, *arg;
                int i;

                if (!event) {
                        g_main_loop_quit(main_loop);
                        return NULL;
                }

                char *input = event->cmd;

                if (!input) {
                    g_main_loop_quit(main_loop);
                    return NULL;
                }

                if (!strlen(input))
                        goto done;

                if (agent_input(dbus_conn, input) == TRUE)
                        goto done;


                cmd = strtok_r(input, " ", &arg);
                if (!cmd)
                        goto done;

                if (arg) {
                        int len = strlen(arg);
                        if (len > 0 && arg[len - 1] == ' ')
                                arg[len - 1] = '\0';
                }

                for (i = 0; cmd_table[i].cmd; i++) {
                        if (strcmp(cmd, cmd_table[i].cmd))
                                continue;

                        if (cmd_table[i].func) {
                                cmd_table[i].func(arg);
                                goto done;
                        }
                }

                if (strcmp(cmd, "help")) {
                        LOG("Invalid command\n");
                        goto done;
                }

                LOG("Available commands:\n");

                for (i = 0; cmd_table[i].cmd; i++) {
                        if (cmd_table[i].desc)
                                LOG("  %s %-*s %s\n", cmd_table[i].cmd,
                                       (int)(25 - strlen(cmd_table[i].cmd)),
                                       cmd_table[i].arg ? : "",
                                       cmd_table[i].desc ? : "");
                }

done:
                bt_cmd_free (event);
        }

        return NULL;
}

static gboolean signal_handler(GIOChannel *channel, GIOCondition condition,
							gpointer user_data)
{
	static bool terminated = false;
	struct signalfd_siginfo si;
	ssize_t result;
	int fd;

	if (condition & (G_IO_NVAL | G_IO_ERR | G_IO_HUP)) {
		g_main_loop_quit(main_loop);
		return FALSE;
	}

	fd = g_io_channel_unix_get_fd(channel);

	result = read(fd, &si, sizeof(si));
	if (result != sizeof(si))
		return FALSE;

	switch (si.ssi_signo) {
	case SIGINT:
		if (input) {
                    /*
			rl_replace_line("", 0);
			rl_crlf();
			rl_on_new_line();
			rl_redisplay();
                    */
			break;
		}

		/*
		 * If input was not yet setup up that means signal was received
		 * while daemon was not yet running. Since user is not able
		 * to terminate client by CTRL-D or typing exit treat this as
		 * exit and fall through.
		 */
	case SIGTERM:
		if (!terminated) {
                    /*
			rl_replace_line("", 0);
			rl_crlf();
                    */
			g_main_loop_quit(main_loop);
		}

		terminated = true;
		break;
	}

	return TRUE;
}

static guint setup_signalfd(void)
{
	GIOChannel *channel;
	guint source;
	sigset_t mask;
	int fd;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
		perror("Failed to set signal mask");
		return 0;
	}

	fd = signalfd(-1, &mask, 0);
	if (fd < 0) {
		perror("Failed to create signal descriptor");
		return 0;
	}

	channel = g_io_channel_unix_new(fd);

	g_io_channel_set_close_on_unref(channel, TRUE);
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);

	source = g_io_add_watch(channel,
				G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
				signal_handler, NULL);

	g_io_channel_unref(channel);

	return source;
}

static gboolean option_version = FALSE;

static gboolean parse_agent(const char *key, const char *value,
					gpointer user_data, GError **error)
{
	if (value)
		auto_register_agent = g_strdup(value);
	else
		auto_register_agent = g_strdup("");

	return TRUE;
}

static GOptionEntry options[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &option_version,
				"Show version information and exit" },
	{ "agent", 'a', G_OPTION_FLAG_OPTIONAL_ARG,
				G_OPTION_ARG_CALLBACK, parse_agent,
				"Register agent handler", "CAPABILITY" },
	{ NULL },
};

static void client_ready(GDBusClient *client, void *user_data)
{
    /*
	if (!input)
		input = setup_standard_input();
    */
}

static int read_config(char *config)
{
        GKeyFile *keyfile;
        GKeyFileFlags flags;
        GError *error = NULL;
        gsize length;
        gchar *url;

        // Create a new GKeyFile object and a bitwise list of flags.
        keyfile = g_key_file_new ();
        flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;

        // Load the GKeyFile from keyfile.conf or return.
        if (!g_key_file_load_from_file (keyfile, config, flags, &error)) {
                g_error (error->message);
                return -1;
        }
        printf("[URL]\n");
        url = g_key_file_get_string(keyfile, "URL", "HTTP_SERVER", NULL);
        printf("URL:%s\n", url);

        snprintf(URL, strlen(url)+1, "%s", url);
}

int main(int argc, char *argv[])
{
	GOptionContext *context;
	GError *error = NULL;
	GDBusClient *client;
	guint signal;

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, options, NULL);

	if (g_option_context_parse(context, &argc, &argv, &error) == FALSE) {
		if (error != NULL) {
			g_printerr("%s\n", error->message);
			g_error_free(error);
		} else
			g_printerr("An unknown error occurred\n");
		exit(1);
	}

	g_option_context_free(context);

	if (option_version == TRUE) {
		printf("%s\n", VERSION);
		exit(0);
	}

        //get_mac("eno1", mac);
        get_mac(argv[1], mac);
        //gat_mac("")
        read_config(CONFIG_FILE);

        srand(time(NULL));

        /*
        struct http_response *resp = self_check();

        struct http_response *oxyresp = send_data(1, "88:C2:55:BB:CC:DD", "67;99");

        struct http_response *glucose_resp = send_data(2, "88:C2:55:BC:73:AF", "4.9");

        struct http_response *blood_resp = send_data(3, "8C:DE:52:FB:C8:CE", "79;22.1;15.6");
        */

	main_loop = g_main_loop_new(NULL, FALSE);
	dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);
        async_queue = g_async_queue_new ();
        state_thread = g_thread_new("state thread", state_handle, async_queue);
        device_hash = g_hash_table_new_full(g_str_hash,
                                            g_str_equal,
                                            release_key,
                                            release_value);
/*
         thread_pool = g_thread_pool_new ((GFunc)test_thread_pools_entry_func,
                                          NULL,
                                          MAX_THREADS,
                                          FALSE,
                                          NULL);
*/

	setlinebuf(stdout);
        /*
	rl_attempted_completion_function = cmd_completion;

	rl_erase_empty_line = 1;
	rl_callback_handler_install(NULL, rl_handler);

	rl_set_prompt(PROMPT_OFF);
	rl_redisplay();
        */
        cmd_queue = g_async_queue_new ();
        cmd_thread = g_thread_new("cmd thread", cmd_handle, cmd_queue);

        /*
        legatt_queue = g_async_queue_new ();
        legatt_thread = g_thread_new("legatt thread", legatt_handle, legatt_queue);
        */

	signal = setup_signalfd();
	client = g_dbus_client_new(dbus_conn, "org.bluez", "/org/bluez");

	g_dbus_client_set_connect_watch(client, connect_handler, NULL);
	g_dbus_client_set_disconnect_watch(client, disconnect_handler, NULL);
	g_dbus_client_set_signal_watch(client, message_handler, NULL);

	g_dbus_client_set_proxy_handlers(client, proxy_added, proxy_removed,
							property_changed, NULL);

	g_dbus_client_set_ready_watch(client, client_ready, NULL);

        /* pooling the device status per 5s */
        //g_timeout_add_seconds(5, recurser_start, NULL);

        /* create server/client socket */
        server_fd = create_server_sock(SERVER);
        server_io = g_io_channel_unix_new(server_fd);
        guint server_source = g_io_add_watch(server_io,
                                             G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
                                             server_handler, NULL);
        client_fd = create_client_sock(CLIENT);

        /* sniffer xingxiao device */
        LOG("[Sock] S -> C: server_fd %d, %s\n", server_fd, "sniffer");
        sock_send_cmd(client_fd, CLIENT, "sniffer", strlen("sniffer")+1);

	g_main_loop_run(main_loop);

	g_dbus_client_unref(client);
	g_source_remove(signal);
	if (input > 0)
		g_source_remove(input);

	//rl_message("");
	//rl_callback_handler_remove();

	dbus_connection_unref(dbus_conn);
	g_main_loop_unref(main_loop);

	g_list_free_full(ctrl_list, proxy_leak);

	g_free(auto_register_agent);

	return 0;
}
