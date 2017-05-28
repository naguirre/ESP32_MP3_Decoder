#ifndef INCLUDE_SSL_CLIENT_H_
#define INCLUDE_SSL_CLIENT_H_

int https_get(const char* hostname, unsigned short port, const char *url, char *buf, int buflen);

#endif /* INCLUDE_SSL_CLIENT_H_ */
