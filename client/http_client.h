/*
	http-client-c
	Copyright (C) 2012-2013  Swen Kooij

	This file is part of http-client-c.

    http-client-c is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    http-client-c is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with http-client-c. If not, see <http://www.gnu.org/licenses/>.

	Warning:
	This library does not tend to work that stable nor does it fully implent the
	standards described by IETF. For more information on the precise implentation of the
	Hyper Text Transfer Protocol:

	http://www.ietf.org/rfc/rfc2616.txt
*/
#ifndef HTTP_CLIENT_H_
#define HTTP_CLIENT_H_

/*
	Represents an HTTP html response
*/
struct http_response
{
	struct parsed_url *request_uri;
	char *body;
	char *status_code;
	int status_code_int;
	char *status_text;
	char *request_headers;
	char *response_headers;
};

/*
	Prototype functions
*/
struct http_response *http_req(char *http_headers, struct parsed_url *purl);
struct http_response *http_get(char *url, char *custom_headers);
struct http_response *http_head(char *url, char *custom_headers);
struct http_response *http_post(char *url, char *custom_headers, char *post_data);
#endif

