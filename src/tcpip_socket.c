#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <errno.h>

#include "tcpip_socket.h"
#include "cros_defs.h"
#include "cros_log.h"
#include "cros_clock.h"

#define TCPIP_SOCKET_READ_BUFFER_SIZE 2048
// Definitions for debug messages only:
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

void tcpIpSocketInit ( TcpIpSocket *s )
{
  PRINT_VDEBUG ( "tcpIpSocketInit()\n" );
  s->fd = -1;
  s->port = 0;
  memset ( & ( s->adr ), 0, sizeof ( struct sockaddr_in ) );
  s->open = 0;
  s->connected = 0;
  s->listening = 0;
  s->is_nonblocking = 0;
}

int tcpIpSocketOpen ( TcpIpSocket *s )
{
  int ret_success;
  PRINT_VDEBUG ( "tcpIpSocketOpen()\n" );
  if ( s->open )
    return 1;

  s->fd = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
  if ( s->fd == FN_INVALID_SOCKET )
  {
    PRINT_ERROR ( "tcpIpSocketOpen() : Can't open a socket\n" );
    s->fd = -1;
    ret_success = 0;
  }
  else
  {
    s->open = 1;
    ret_success = 1;
  }

  return ( ret_success );
}

void tcpIpSocketClose ( TcpIpSocket *s )
{
  PRINT_VDEBUG ( "tcpIpSocketClose()\n");

  if ( !s->open )
    return;

  PRINT_DEBUG ( "tcpIpSocketClose(): Closing socket FD: %i\n", s->fd);
  if ( s->fd != -1 )
    close ( s->fd );
  else
    PRINT_ERROR ( "tcpIpSocketClose(): Invalid file descriptor: %i\n", s->fd);

  tcpIpSocketInit ( s );
}

int tcpIpSocketSetNonBlocking ( TcpIpSocket *s )
{
  int prev_flags;

  PRINT_VDEBUG ( "tcpIpSocketSetNonBlocking()\n" );

  if ( !s->open )
  {
    PRINT_ERROR ( "tcpIpSocketSetNonBlocking() : Socket not opened\n" );
    return 0;
  }

  prev_flags = fcntl( s->fd, F_GETFL, 0 );
  if(prev_flags < 0)
  {
    PRINT_ERROR ( "tcpIpSocketSetNonBlocking() : fcntl() failed getting the socket flags\n" );
    prev_flags = 0; // Try to continue anyway
  }

  int ret = fcntl ( s->fd, F_SETFL, prev_flags | O_NONBLOCK );
  if ( ret == 0 )
  {
    s->is_nonblocking = 1;
    return 1;
  }
  else
  {
    PRINT_ERROR ( "tcpIpSocketSetNonBlocking() : fcntl() failed configuring socket as non blocking\n" );
    return 0;
  }
}

int tcpIpSocketSetNoDelay ( TcpIpSocket *s )
{
  PRINT_VDEBUG ( "tcpIpSocketSetNoDelay()\n" );

  if ( !s->open )
  {
    PRINT_ERROR ( "tcpIpSocketSetNoDelay() : Socket not opened\n" );
    return 0;
  }

  int val = 1;
  int ret = setsockopt ( s->fd, IPPROTO_TCP, TCP_NODELAY, ( const void * ) ( &val ), sizeof ( int ) );

  if ( ret == 0 )
  {
    return 1;
  }
  else
  {
    PRINT_ERROR ( "tcpIpSocketSetNoDelay() : setsockopt() with TCP_NODELAY failed. System error number: %i \n", errno );
    return 0;
  }
}

int tcpIpSocketSetReuse ( TcpIpSocket *s )
{
  PRINT_VDEBUG ( "tcpIpSocketSetReuse()\n" );

  if ( !s->open )
  {
    PRINT_ERROR ( "tcpIpSocketSetReuse() : Socket not opened\n" );
    return 0;
  }

  int val = 1;
  if ( setsockopt ( s->fd, SOL_SOCKET, SO_REUSEADDR, ( const char* ) ( &val ), sizeof ( int ) ) != 0 )
  {
    PRINT_ERROR ( "tcpIpSocketSetReuse() : setsockopt() with SO_REUSEADDR option failed \n" );
    return 0;
  }
  return 1;
}

int tcpIpSocketSetKeepAlive ( TcpIpSocket *s, unsigned int idle, unsigned int interval, unsigned int count )
{
  PRINT_VDEBUG ( "tcpIpSocketSetKeepAlive()\n" );

  if ( !s->open )
  {
    PRINT_ERROR ( "tcpIpSocketSetKeepAlive() : Socket not opened\n" );
    return 0;
  }

  int val = 1;
  if ( setsockopt ( s->fd, SOL_SOCKET, SO_KEEPALIVE, ( const char* ) ( &val ), sizeof ( int ) ) != 0 )
  {
    PRINT_ERROR ( "tcpIpSocketSetKeepAlive() : setsockopt() with SO_KEEPALIVE option failed \n" );
    return 0;
  }

  // TCP_KEEPIDLE on Linux is equivalent to TCP_KEEPALIVE option on OSX
  // see https://www.winehq.org/pipermail/wine-devel/2015-July/108583.html
  val = idle;
#ifdef __APPLE__
  if ( setsockopt( s->fd, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof ( val ) ) != 0 )
  {
    PRINT_ERROR ( "tcpIpSocketSetKeepAlive() : setsockopt() with TCP_KEEPALIVE option failed \n" );
    return 0;
  }
#else
  if ( setsockopt ( s->fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof ( val ) ) != 0 )
  {
    PRINT_ERROR ( "tcpIpSocketSetKeepAlive() : setsockopt() with SO_KEEPALIVE option failed \n" );
    return 0;
  }
#endif

  val = interval;
  if ( setsockopt ( s->fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof ( val ) ) != 0 )
  {
    PRINT_ERROR ( "tcpIpSocketSetKeepAlive() : setsockopt() with TCP_KEEPINTVL option failed \n" );
    return 0;
  }

  val = count;
  if ( setsockopt ( s->fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof ( val ) ) != 0 )
  {
    PRINT_ERROR ( "tcpIpSocketSetKeepAlive() : setsockopt() with TCP_KEEPCNT option failed \n" );
    return 0;
  }

  return 1;
}

TcpIpSocketState tcpIpSocketConnect ( TcpIpSocket *s, const char *host_addr, unsigned short host_port )
{
  int connect_ret;
  PRINT_VDEBUG ( "tcpIpSocketConnect():\n" );

  if ( !s->open )
  {
    PRINT_ERROR ( "tcpIpSocketConnect() : Socket not opened\n" );
    return TCPIPSOCKET_FAILED;
  }

  if( s->connected )
    return TCPIPSOCKET_DONE;

  struct sockaddr_in adr;

  memset ( &adr, 0, sizeof ( struct sockaddr_in ) );

  adr.sin_family = AF_INET;
  adr.sin_port = htons ( host_port );
  if ( inet_pton ( AF_INET, host_addr, &adr.sin_addr ) <= 0 )
  {
    PRINT_ERROR ( "tcpIpSocketConnect() : Can't get a valid address from %s\n", host_addr );
    s->connected = 0;
    return TCPIPSOCKET_FAILED;
  }

  connect_ret = connect ( s->fd, ( struct sockaddr * ) &adr, sizeof ( struct sockaddr ) );
  if ( connect_ret == -1 && errno != EISCONN ) // The connection is not established so far
  {
    if ( s->is_nonblocking &&
       ( errno == EINPROGRESS || errno == EALREADY ) )
    {
      PRINT_DEBUG ( "tcpIpSocketConnect() : Connection in progress to %s:%i through FD:%i\n", host_addr, host_port, s->fd);
      return TCPIPSOCKET_IN_PROGRESS;
    }
    else
    {
      s->connected = 0;
      if(errno == ECONNREFUSED)
      {
         PRINT_ERROR ( "tcpIpSocketConnect() : Connection to %s:%i through FD:%i was refused\n", host_addr, host_port, s->fd);
         return TCPIPSOCKET_REFUSED;
      }
      else
      {
         PRINT_ERROR ( "tcpIpSocketConnect() : Connection to %s:%i through FD:%i failed due to error errno=%i\n", host_addr, host_port, s->fd, errno);
         return TCPIPSOCKET_FAILED;
      }
    }
  }
  PRINT_DEBUG ( "tcpIpSocketConnect() : connection done to %s:%i through FD:%i\n", host_addr, host_port, s->fd);

  s->port = host_port;
  s->adr = adr;
  s->connected = 1;

  return TCPIPSOCKET_DONE;
}

int tcpIpSocketDisconnect ( TcpIpSocket *s )
{
  PRINT_VDEBUG ( "tcpIpSocketDisconnect()\n" );

  if ( !s->connected )
    return 1;

  s->connected = 0;
  if ( shutdown ( s->fd, SHUT_RDWR ) == -1 )
  {
    PRINT_ERROR ( "tcpIpSocketDisconnect() : shutdown failed\n" );
    return 0;
  }
  return 1;
}

TcpIpSocketState tcpIpSocketCheckPort ( const char *host_addr, unsigned short host_port )
{
   TcpIpSocketState port_open, fn_ret;
   TcpIpSocket socket_struct;

   tcpIpSocketInit ( &socket_struct );
   fn_ret = tcpIpSocketOpen ( &socket_struct );
   if(fn_ret)
   {
      TcpIpSocketState socket_stat;
      socket_stat = tcpIpSocketConnect ( &socket_struct, host_addr, host_port );
      if ( socket_stat == TCPIPSOCKET_IN_PROGRESS )
         port_open = TCPIPSOCKET_FAILED;
      else
      {
         port_open = socket_stat;
         if ( socket_stat == TCPIPSOCKET_DONE )
            tcpIpSocketDisconnect (  &socket_struct );
      }

      tcpIpSocketClose ( &socket_struct );
   }
   else
      port_open = TCPIPSOCKET_FAILED;

   return port_open;
}

int tcpIpSocketBindListen( TcpIpSocket *s, const char *host, unsigned short port, int backlog )
{
  PRINT_VDEBUG ( "tcpIpSocketBindListen()\n" );

  if ( !s->open )
  {
    PRINT_ERROR ( "tcpIpSocketBindListen() : Socket not opened\n" );
    return 0;
  }

  if ( !s->listening )
  {
    struct sockaddr_in adr;

    memset ( &adr, 0, sizeof ( struct sockaddr_in ) );

    adr.sin_family = AF_INET;
    adr.sin_port = htons ( port );
    adr.sin_addr.s_addr = htonl(INADDR_ANY);

    if ( inet_pton ( AF_INET, host, &adr.sin_addr ) <= 0 )
    {
      PRINT_ERROR ( "tcpIpSocketBindListen() : Can't get a valid addres from %s\n", host );
      return 0;
    }


    if ( bind ( s->fd, ( struct sockaddr * ) &adr, sizeof ( struct sockaddr ) ) == -1 )
    {
      PRINT_ERROR ( "tcpIpSocketBindListen() : Bind failed\n" );
      return 0;
    }

    if ( listen ( s->fd, backlog ) == -1 )
    {
      PRINT_ERROR ( "tcpIpSocketBindListen() : Listen failed\n" );
      return 0;
    }

    struct sockaddr sa;
    socklen_t sa_len = sizeof( struct sockaddr );
    if ( getsockname(s->fd, (struct sockaddr *)&sa, &sa_len) == -1 )
    {
      PRINT_ERROR ( "tcpIpSocketBindListen() : getsockname() failed\n" );
      return 0;
    }

    struct sockaddr_in *sin = (struct sockaddr_in *)&sa;

    s->port = ntohs(sin->sin_port);
    s->adr = adr;
    s->listening = 1;
  }

  return 1;
}

TcpIpSocketState tcpIpSocketAccept ( TcpIpSocket *s, TcpIpSocket *new_s )
{
  PRINT_VDEBUG ( "tcpIpSocketAccept()\n" );

  TcpIpSocketState state = TCPIPSOCKET_DONE;

  if ( !s->open || !s->listening )
  {
    PRINT_ERROR ( "tcpIpSocketAccept() : Socket not opened or not listening\n" );
    return TCPIPSOCKET_FAILED;
  }

  struct sockaddr_in new_adr;
  socklen_t new_adr_len = sizeof(struct sockaddr);

  int new_fd = accept ( s->fd, ( struct sockaddr * ) &new_adr, &new_adr_len );

  if ( new_fd == -1 )
  {
    if ( s->is_nonblocking &&
       ( errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EAGAIN ) )
    {
      PRINT_DEBUG ( "tcpIpSocketAccept() : Accept in progress from port %i, new FD:%i\n", new_adr.sin_port, new_fd);
      state = TCPIPSOCKET_IN_PROGRESS;
    }
    else
    {
      PRINT_ERROR ( "tcpIpSocketAccept() : accept() failed, errno: %d\n", errno );
      return TCPIPSOCKET_FAILED;
    }
  }

  if( new_s->open )
    tcpIpSocketClose ( new_s );

  new_s->fd = new_fd;
  new_s->adr = new_adr;
  new_s->port = s->port;
  new_s->open = 1;
  new_s->connected = 1;

  return state;
}

void printTransmissionBuffer(const char *buffer, const char *msg_info, int msg_fd, int buf_len)
{
  int i;
  PRINT_DEBUG(ANSI_COLOR_CYAN"\n%s (%i bytes fd: %i) ["ANSI_COLOR_RESET, msg_info, buf_len, msg_fd);
  for(i=0;i<buf_len;i++)
  {
    char ch=buffer[i];
    if(ch=='\0' || ch=='\t' || ch=='\r' || ch=='\n' || ch=='\a')
      ch='.';
    PRINT_DEBUG("%c",ch);
  }
  PRINT_DEBUG(ANSI_COLOR_CYAN"]\n"ANSI_COLOR_RESET);
}

TcpIpSocketState tcpIpSocketWriteBuffer ( TcpIpSocket *s, DynBuffer *d_buf )
{
  PRINT_VDEBUG ( "tcpIpSocketWriteBuffer()\n" );

  const unsigned char *data = dynBufferGetCurrentData ( d_buf );
  int data_size = dynBufferGetRemainingDataSize ( d_buf );

  if ( !s->connected )
  {
    PRINT_ERROR ( "tcpIpSocketWriteBuffer() : Socket not connected\n" );
    return TCPIPSOCKET_FAILED;
  }

  #if CROS_DEBUG_LEVEL >= 2
  printTransmissionBuffer((const char *)data, "tcpIpSocketWriteBuffer() : Buffer", s->fd, data_size);
  #endif
  while ( data_size > 0 )
  {
    int n_written = send ( s->fd, ( void * ) data, data_size, 0 );

    if ( n_written > 0 )
    {
      dynBufferMovePoseIndicator ( d_buf, n_written );
      data = dynBufferGetCurrentData ( d_buf );
      data_size = dynBufferGetRemainingDataSize ( d_buf );
    }
    else if ( s->is_nonblocking &&
              ( errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EAGAIN ) )
    {
      PRINT_DEBUG ( "tcpIpSocketWriteBuffer() : write in progress, %d remaining bytes\n", data_size );
      return TCPIPSOCKET_IN_PROGRESS;
    }
    else if ( errno == ENOTCONN || errno == ECONNRESET )
    {
      PRINT_DEBUG ( "tcpIpSocketWriteBuffer() : socket disconnected\n" );
      s->connected = 0;
      return  TCPIPSOCKET_DISCONNECTED;
    }
    else
    {
      PRINT_ERROR ( "tcpIpSocketWriteBuffer() : Write failed. errno: %i\n" ,errno);
      return TCPIPSOCKET_FAILED;
    }
  }

  return TCPIPSOCKET_DONE;
}

TcpIpSocketState tcpIpSocketWriteString ( TcpIpSocket *s, DynString *d_str )
{
  PRINT_VDEBUG ( "tcpIpSocketWriteString()\n" );

  const char *data = dynStringGetCurrentData ( d_str );
  int data_size = dynStringGetRemainingDataSize ( d_str );

  if ( !s->connected )
  {
    PRINT_ERROR ( "tcpIpSocketWriteString() : Socket not connected\n" );
    return TCPIPSOCKET_FAILED;
  }
  #if CROS_DEBUG_LEVEL >= 2
  printTransmissionBuffer(data, "tcpIpSocketWriteString() : Buffer", s->fd, data_size);
  #endif
  while ( data_size > 0 )
  {
    int n_written = send ( s->fd, ( void * ) data, data_size, 0 );

    if ( n_written > 0 )
    {
      dynStringMovePoseIndicator ( d_str, n_written );
      data = dynStringGetCurrentData ( d_str );
      data_size = dynStringGetRemainingDataSize ( d_str );
    }
    else if ( s->is_nonblocking &&
              ( errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EAGAIN ) )
    {
      PRINT_DEBUG ( "tcpIpSocketWriteString() : write in progress, %d remaining bytes\n", data_size );
      return TCPIPSOCKET_IN_PROGRESS;
    }
    else if ( errno == ENOTCONN || errno == ECONNRESET )
    {
      PRINT_DEBUG ( "tcpIpSocketWriteString() : socket disconnectd\n" );
      s->connected = 0;
      return  TCPIPSOCKET_DISCONNECTED;
    }
    else
    {
      PRINT_ERROR ( "tcpIpSocketWriteString() : Write failed\n" );
      return TCPIPSOCKET_FAILED;
    }
  }

  return TCPIPSOCKET_DONE;
}

TcpIpSocketState tcpIpSocketReadBuffer ( TcpIpSocket *s, DynBuffer *d_buf )
{
  size_t n_read;
  return tcpIpSocketReadBufferEx(s, d_buf, TCPIP_SOCKET_READ_BUFFER_SIZE, &n_read);
}

TcpIpSocketState tcpIpSocketReadBufferEx( TcpIpSocket *s, DynBuffer *d_buf, size_t max_size, size_t *n_reads)
{
  PRINT_VDEBUG ( "tcpIpSocketReadBufferEx()\n" );

  *n_reads = 0;
  if ( !s->connected )
  {
    PRINT_ERROR ( "tcpIpSocketReadBufferEx() : Socket not connected\n" );
    return TCPIPSOCKET_FAILED;
  }

  unsigned char *read_buf = (unsigned char *)malloc(max_size);
  if (!read_buf)
  {
    PRINT_ERROR("tcpIpSocketReadBufferEx() : Out of memory while reading from socket");
    return TCPIPSOCKET_FAILED;
  }

  TcpIpSocketState state = TCPIPSOCKET_UNKNOWN;
  int reads = recv ( s->fd, read_buf, max_size, 0);
  if ( reads == 0 )
  {
    PRINT_DEBUG ( "tcpIpSocketReadBufferEx() : socket disconnectd\n" );
    s->connected = 0;
    state = TCPIPSOCKET_DISCONNECTED;
  }
  else if ( reads > 0 )
  {
    PRINT_DEBUG ( "tcpIpSocketReadBufferEx() : read %d bytes \n", reads );
    #if CROS_DEBUG_LEVEL >= 2
    printTransmissionBuffer((const char *)read_buf, "tcpIpSocketReadBufferEx() : Buffer", s->fd, reads);
    #endif

    dynBufferPushBackBuf ( d_buf, read_buf, reads );
    state = TCPIPSOCKET_DONE;
    *n_reads = reads;
  }
  else if ( s->is_nonblocking &&
            ( errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EAGAIN ) )
  {
    PRINT_DEBUG ( "tcpIpSocketReadBufferEx() : read in progress\n" );
    state = TCPIPSOCKET_IN_PROGRESS;
  }
  else if ( errno == ENOTCONN || errno == ECONNRESET )
  {
    PRINT_DEBUG ( "tcpIpSocketReadBufferEx() : socket disconnectd\n" );
    s->connected = 0;
    state = TCPIPSOCKET_DISCONNECTED;
  }
  else
  {
    PRINT_ERROR ( "tcpIpSocketReadBufferEx() : Read failed\n" );
    state = TCPIPSOCKET_FAILED;
  }

  free(read_buf);

  return state;
}

TcpIpSocketState tcpIpSocketReadString ( TcpIpSocket *s, DynString *d_str )
{
  PRINT_VDEBUG ( "tcpIpSocketReadString()\n" );

  if ( !s->connected )
  {
    PRINT_ERROR ( "tcpIpSocketReadString() : Socket not connected\n" );
    return TCPIPSOCKET_FAILED;
  }

  char read_buf[TCPIP_SOCKET_READ_BUFFER_SIZE];

  TcpIpSocketState state = TCPIPSOCKET_UNKNOWN;

  int n_read = recv ( s->fd, read_buf, TCPIP_SOCKET_READ_BUFFER_SIZE , 0 );

  if ( n_read == 0 )
  {
    PRINT_DEBUG ( "tcpIpSocketReadString() : socket disconnectd\n" );
    s->connected = 0;
    state = TCPIPSOCKET_DISCONNECTED;
  }
  else if ( n_read > 0 )
  {
    PRINT_DEBUG ( "tcpIpSocketReadString() : read %d bytes \n", n_read );
    #if CROS_DEBUG_LEVEL >= 2
    printTransmissionBuffer(read_buf, "tcpIpSocketReadString() : Buffer", s->fd, n_read);
    #endif

    dynStringPushBackStrN ( d_str, read_buf, n_read );
    state = TCPIPSOCKET_DONE;
  }
  else if ( s->is_nonblocking &&
            ( errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EAGAIN ) )
  {
    PRINT_DEBUG ( "tcpIpSocketReadString() : read in progress\n" );
    state = TCPIPSOCKET_IN_PROGRESS;
  }
  else if ( errno == ENOTCONN || errno == ECONNRESET )
  {
    PRINT_DEBUG ( "tcpIpSocketReadString() : socket disconnectd\n" );
    s->connected = 0;
    state = TCPIPSOCKET_DISCONNECTED;
  }
  else
  {
    PRINT_ERROR ( "tcpIpSocketReadString() : Read failed\n" );
    state = TCPIPSOCKET_FAILED;
  }

  return state;
}

int tcpIpSocketGetFD ( TcpIpSocket *s )
{
  return s->fd;
}

unsigned short tcpIpSocketGetPort( TcpIpSocket *s )
{
  return s->port;
}

int tcpIpSocketSelect( int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, uint64_t time_out )
{
  struct timeval timeout_tv;
  int nfds_set;

  timeout_tv = cRosClockGetTimeVal( time_out );

  nfds_set = select(nfds, readfds, writefds, exceptfds, &timeout_tv);

  if(nfds_set == FN_SOCKET_ERROR) // It is not needed but it is recommended on Windows
    nfds_set = -1;

  if(nfds_set == -1)
  {
    int socket_err_num;

    socket_err_num = tcpIpSocketGetError();

    if(socket_err_num == EINTR)
    {
      PRINT_DEBUG("tcpIpSocketSelect() : select() returned EINTR error code\n");
      nfds_set = 0;
    }
  }

  return(nfds_set);
}

int tcpIpSocketGetError( void )
{
  int socket_err_num;

#ifdef _WIN32
  socket_err_num = WSAGetLastError();
#else
  socket_err_num = errno;
#endif

  return(socket_err_num);
}
