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

static void hexdump(void *buf, long size)
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

static int str2uuid(const char *uuid_str, uuid_t *uuid)
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


char cmd1[] = {0xcc, 0x96, 0x02, 0x03, 0x01, 0x01, 0x00, 0x01};
char cmd2[] = {0xcc, 0x96, 0x02, 0x03, 0x01, 0x02, 0x00, 0x02};

int spp_connet(char *baddr)
{
        sdp_session_t *session = NULL;
        int retries = 0;
        int chan = 0;
        int responses;
        sdp_list_t *response_list = NULL, *search_list, *attrid_list;
        struct sockaddr_rc loc_addr = { 0 };
        uuid_t  uuid = { 0 };
        char *uuid_str="00001101-0000-1000-8000-00805f9b34fb";
        uint32_t range = 0x0000ffff;
        bdaddr_t target;
        int s;
        int err;
        int status;

        if (!str2uuid(uuid_str, &uuid)) {
                LOG("Invalid UUID");
                exit(1);
        }

        str2ba(baddr, &target);

        while (!session) {
                session = sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);
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
        for ( ; r; r=r->next) {
                rec = (sdp_record_t *)r->data;
                sdp_list_t *protos;

                /* get a list of the protocol sequences */
                if (sdp_get_access_protos(rec, &protos) == 0) {
                        chan = sdp_get_proto_port(protos, RFCOMM_UUID);
                        sdp_list_foreach(protos, (sdp_list_func_t) sdp_list_free,
                                         NULL);
                        sdp_list_free(protos, NULL);
                        if (chan > 0)
                                break;
                }
                sdp_record_free(rec);
        }

        if (chan > 0) {
                LOG("Found service on this device, now send the cmd\n");
                s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
                loc_addr.rc_family = AF_BLUETOOTH;
                loc_addr.rc_channel = chan;
                str2ba(baddr, &loc_addr.rc_bdaddr);
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
        }

        sdp_list_free(response_list, 0);
        sdp_list_free(search_list, 0);
        sdp_list_free(attrid_list, 0);
        sdp_close(session);
}

