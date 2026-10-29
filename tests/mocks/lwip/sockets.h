#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#define lwip_socket socket
#define lwip_fcntl fcntl
#define lwip_connect connect
#define lwip_select select
#define lwip_getsockopt getsockopt
#define lwip_send send
#define lwip_recv recv
#define lwip_close close
#define lwip_htons htons
