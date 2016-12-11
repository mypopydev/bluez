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
#ifndef URLPARSER_H_
#define URLPARSER_H_
/*
	Represents an url
*/
struct parsed_url{
        char *uri;      /* mandatory */
        char *scheme;   /* mandatory */
        char *host;     /* mandatory */
        char *ip;       /* mandatory */

        char *port;     /* optional  */
        char *path;     /* optional  */
        char *query;    /* optional  */
        char *fragment; /* optional  */
        char *username; /* optional  */
        char *password; /* optional  */
};

/*
	Free memory of parsed url
*/
void parsed_url_free(struct parsed_url *purl);

/*
	Retrieves the IP adress of a hostname
*/
char* hostname_to_ip(char *hostname);

/*
	Check whether the character is permitted in scheme string
*/
int is_scheme_char(int c);

/*
	Parses a specified URL and returns the structure named 'parsed_url'
	Implented according to:
	RFC 1738 - http://www.ietf.org/rfc/rfc1738.txt
	RFC 3986 -  http://www.ietf.org/rfc/rfc3986.txt
*/
struct parsed_url *parse_url(const char *url);
#endif
