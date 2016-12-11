#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>

int str2uuid(const char *uuid_str, uuid_t *uuid)
{
	uint32_t uuid_int[4];
	char *endptr;

	if (strlen(uuid_str) == 36) {
		/* Parse uuid128 standard format: 12345678-9012-3456-7890-123456789012 */
		char buf[9] = { 0 };

		if (uuid_str[8] != '-' && uuid_str[13] != '-' &&
		    uuid_str[18] != '-'  && uuid_str[23] != '-') {
			return 0;
		}
		/* first 8-bytes */
		strncpy(buf, uuid_str, 8);
		uuid_int[0] = htonl(strtoul(buf, &endptr, 16));
		if (endptr != buf + 8) return 0;

		/* second 8-bytes */
		strncpy(buf, uuid_str+9, 4);
		strncpy(buf+4, uuid_str+14, 4);
		uuid_int[1] = htonl(strtoul(buf, &endptr, 16));
		if (endptr != buf + 8) return 0;

		/* third 8-bytes */
		strncpy(buf, uuid_str+19, 4);
		strncpy(buf+4, uuid_str+24, 4);
		uuid_int[2] = htonl(strtoul(buf, &endptr, 16));
		if (endptr != buf + 8) return 0;

		/* fourth 8-bytes */
		strncpy(buf, uuid_str+28, 8);
		uuid_int[3] = htonl(strtoul(buf, &endptr, 16));
		if (endptr != buf + 8) return 0;

		if (uuid != NULL) sdp_uuid128_create(uuid, uuid_int);
	} else if (strlen(uuid_str) == 8) {
		/* 32-bit reserved UUID */
		uint32_t i = strtoul(uuid_str, &endptr, 16);
		if (endptr != uuid_str + 8) return 0;
		if (uuid != NULL) sdp_uuid32_create(uuid, i);
	} else if (strlen( uuid_str) == 4) {
		/* 16-bit reserved UUID */
		int i = strtol(uuid_str, &endptr, 16);
		if (endptr != uuid_str + 4) return 0;
		if (uuid != NULL) sdp_uuid16_create(uuid, i);
	} else {
		return 0;
	}

	return 1;
}

#define NELEMS(array) (sizeof(array) / sizeof(array[0]))

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

void hexdump(void *buf, long size)
{
	char sz_buf[100];
	long indent = 1;
	long out_len, index, out_len2;
	long rel_pos;
	struct {
		char *data;
		unsigned long size;
	} buff;
	unsigned char *tmp,tmp_pos;
	unsigned char *addr = (unsigned char *)buf;

	buff.data = (char *)addr;
	buff.size = size;

	while (buff.size > 0) {
		tmp      = (unsigned char *)buff.data;
		out_len  = (int)buff.size;
		if (out_len > 32)
			out_len = 32;

		/* create a 85-character formatted output line: */
		sprintf(sz_buf, "                              "
			"                              "
			"              [%08lX]", tmp-addr);
		out_len2 = out_len;

		for (index=indent, rel_pos=0; out_len2; out_len2--,index += 2 ) {
			tmp_pos = *tmp++;
			sprintf(sz_buf + index, "%02X ", (unsigned short)tmp_pos);
			if (!(++rel_pos & 3))     /* extra blank after 4 bytes */
				index++;
		}

		if (!(rel_pos & 3)) index--;

		sz_buf[index+1] = ' ';

		printf("%s\n", sz_buf);

		buff.data += out_len;
		buff.size -= out_len;
	}
}

char cmd1[] = {0xcc, 0x96, 0x02, 0x03, 0x01, 0x01, 0x00, 0x01};
char cmd2[] = {0xcc, 0x96, 0x02, 0x03, 0x01, 0x02, 0x00, 0x02};

int main(void)
{
        int i, err, sock, dev_id = -1;
	struct hci_dev_info dev_info;
	inquiry_info *info = NULL;
	int num_rsp, length, flags;
	char addr[19] = { 0 };
	char name[248] = { 0 };
	uuid_t uuid = { 0 };
	/* Change this to your apps UUID */
	char *uuid_str="00001101-0000-1000-8000-00805f9b34fb";
	uint32_t range = 0x0000ffff;
	sdp_list_t *response_list = NULL, *search_list, *attrid_list;
	int s, loco_channel = -1, status;
	struct sockaddr_rc loc_addr = { 0 };

	(void) signal(SIGINT, SIG_DFL);

	dev_id = hci_get_route(NULL);
	if (dev_id < 0) {
		LOG("No Bluetooth Adapter Available");
		exit(1);
	}

	if (hci_devinfo(dev_id, &dev_info) < 0) {
		LOG("Can't get device info");
		exit(1);
	}

	sock = hci_open_dev(dev_id);
	if (sock < 0) {
		LOG("HCI device open failed");
		free(info);
		exit(1);
	}

	if (!str2uuid(uuid_str, &uuid)) {
		LOG("Invalid UUID");
		free(info);
		exit(1);
        }

	do {
		LOG("Scanning ...\n");
		info = NULL;
		num_rsp = 0;
		flags = 0;
		length = 8; /* ~10 seconds */
		num_rsp = hci_inquiry(dev_id, length, num_rsp, NULL, &info, flags);
		if (num_rsp < 0) {
			LOG("Inquiry failed");
			exit(1);
		}

		LOG("Num of resp: %d\n", num_rsp);

		for (i=0; i<num_rsp; i++) {
			sdp_session_t *session;
			int retries;
			int foundit, responses;
			ba2str(&(info+i)->bdaddr, addr);
			memset(name, 0, sizeof(name));
			if (hci_read_remote_name(sock, &(info+i)->bdaddr, sizeof(name),
                                                 name, 0) < 0)
                                strcpy(name, "[unknown]");
			LOG("Found %s - %s, searching for the the desired service on it now\n",
                               addr, name);

			/* connect to the SDP server running on the remote machine */
sdpconnect:
			session = 0; retries = 0;
			while (!session) {
				session = sdp_connect(BDADDR_ANY, &(info+i)->bdaddr, SDP_RETRY_IF_BUSY);
				if (session) break;
				if (errno == EALREADY && retries < 5) {
					LOG("Retrying");
					retries++;
					sleep(1);
					continue;
				}
				break;
			}
			if (session == NULL) {
				LOG("Can't open session with the device");
				free(info);
				continue;
			}
			search_list = sdp_list_append(0, &uuid);
			attrid_list = sdp_list_append(0, &range);
			err = 0;
			err = sdp_service_search_attr_req(session, search_list,
                                                          SDP_ATTR_REQ_RANGE,
                                                          attrid_list, &response_list);
                        if (err) {
                                LOG("Service Search failed: %s\n", strerror(errno));
                                sdp_list_free(attrid_list, 0);
                                sdp_list_free(search_list, 0);
                                sdp_close(session);
                                return -1;
                        }

			sdp_list_t *r = response_list;
			sdp_record_t *rec;
			/* go through each of the service records */
			foundit = 0;
			responses = 0;
			for ( ; r; r=r->next) {
                            responses++;
                            rec = (sdp_record_t*)r->data;
                            sdp_list_t *proto_list;

                            /* get a list of the protocol sequences */
                            if (sdp_get_access_protos(rec, &proto_list) == 0) {
                                sdp_list_t *p = proto_list;

                                /* go through each protocol sequence */
                                for ( ; p; p=p->next) {
                                        sdp_list_t *pds = (sdp_list_t*)p->data;
                                        /* go through each protocol list of the protocol sequence */
                                        for ( ; pds; pds=pds->next) {
                                                /* check the protocol attributes */
                                                sdp_data_t *d = (sdp_data_t*)pds->data;
                                                int proto = 0;
                                                for ( ;d; d=d->next) {
                                                        switch (d->dtd) {
                                                        case SDP_UUID16:
                                                        case SDP_UUID32:
                                                        case SDP_UUID128:
                                                                proto = sdp_uuid_to_proto(&d->val.uuid);
                                                                break;
                                                        case SDP_UINT8:
                                                                if (proto == RFCOMM_UUID) {
                                                                        LOG("rfcomm channel: %d\n",d->val.int8);
                                                                        loco_channel = d->val.int8;
                                                                        foundit = 1;
                                                                }
                                                                break;
                                                        }
                                                }
                                        }
                                        sdp_list_free((sdp_list_t*)p->data, 0);
                                }
                                sdp_list_free(proto_list, 0);
                            }
                            if (loco_channel > 0)
                                    break;
			}
			LOG("Num of Responses %d\n", responses);
			if (loco_channel > 0 && foundit == 1) {
				LOG("Found service on this device, now send the cmd\n");
				s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
				loc_addr.rc_family = AF_BLUETOOTH;
				loc_addr.rc_channel = loco_channel;
				loc_addr.rc_bdaddr = *(&(info+i)->bdaddr);
				status = connect(s, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
				if (status < 0)
					LOG("uh oh");

				status = write(s, cmd1, 8); /* FIXME */
				usleep(100);
				status = write(s, cmd2, 8); /* FIXME */
				LOG ("Wrote %d bytes\n", status);

				char buf[128];
				ssize_t len;
				do {
					len = read(s, buf, 128);
					LOG("read %d bytes\n", len);
					hexdump(buf, len);
				} while (len > 0);
				close(s);
				sdp_record_free(rec);
			}

			sdp_close(session);
			if (loco_channel > 0) {
				goto sdpconnect;
				//break;
			}
		}
	} while (1);

	LOG("Exiting...\n");
}

