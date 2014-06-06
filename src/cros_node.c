#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>

#include "cros_node_internal.h"
#include "cros_node.h"
#include "cros_api.h"
#include "cros_message.h"
#include "cros_clock.h"
#include "cros_defs.h"
#include "cros_node_api.h"

static int enqueueSubscriberAdvertise(CrosNode *node, int subidx);
static int enqueuePubliserAdvertise(CrosNode *node, int pubidx);
static int enqueueServiceAdvertise(CrosNode *node, int servivceidx);

static void openXmlrpcClientSocket( CrosNode *n, int i )
{
  if( !tcpIpSocketOpen( &(n->xmlrpc_client_proc[i].socket) ) ||
      !tcpIpSocketSetReuse( &(n->xmlrpc_client_proc[i].socket) ) ||
      !tcpIpSocketSetNonBlocking( &(n->xmlrpc_client_proc[i].socket) ) )
  {
    PRINT_ERROR("openXmlrpcClientSocket() at index %d failed", i);
    exit( EXIT_FAILURE );
  }
}

static void openTcprosClientSocket( CrosNode *n, int i )
{
  if( !tcpIpSocketOpen( &(n->tcpros_client_proc[i].socket) ) ||
      !tcpIpSocketSetReuse( &(n->tcpros_client_proc[i].socket) ) ||
      !tcpIpSocketSetNonBlocking( &(n->tcpros_client_proc[i].socket) ) )
  {
    PRINT_ERROR("openTcprosClientSocket() at index %d failed", i);
    exit( EXIT_FAILURE );
  }
}

static void openXmlrpcListnerSocket( CrosNode *n )
{
  if( !tcpIpSocketOpen( &(n->xmlrpc_listner_proc.socket) ) ||
      !tcpIpSocketSetReuse( &(n->xmlrpc_listner_proc.socket) ) ||
      !tcpIpSocketSetNonBlocking( &(n->xmlrpc_listner_proc.socket) ) ||
      !tcpIpSocketBindListen( &(n->xmlrpc_listner_proc.socket), n->host, 0, CN_MAX_XMLRPC_SERVER_CONNECTIONS ) )
  {
    PRINT_ERROR("openXmlrpcListnerSocket() failed");
    exit( EXIT_FAILURE );
  }
  else
  {
    n->xmlrpc_port = tcpIpSocketGetPort( &(n->xmlrpc_listner_proc.socket) );
    PRINT_DEBUG ( "openXmlrpcListnerSocket () : Accepting xmlrpc connections at port %d\n", n->xmlrpc_port );
  }
}

static void openRpcrosListnerSocket( CrosNode *n )
{
  if( !tcpIpSocketOpen( &(n->rpcros_listner_proc.socket) ) ||
      !tcpIpSocketSetReuse( &(n->rpcros_listner_proc.socket) ) ||
      !tcpIpSocketSetNonBlocking( &(n->rpcros_listner_proc.socket) ) ||
      !tcpIpSocketBindListen( &(n->rpcros_listner_proc.socket), n->host, 0, CN_MAX_RPCROS_SERVER_CONNECTIONS ) )
  {
    PRINT_ERROR("openRpcrosListnerSocket() failed");
    exit( EXIT_FAILURE );
  }
  else
  {
    n->rpcros_port = tcpIpSocketGetPort( &(n->rpcros_listner_proc.socket) );
    PRINT_DEBUG ( "openRpcrosListnerSocket() : Accepting rcpros connections at port %d\n", n->rpcros_port );
  }
}

static void openTcprosListnerSocket( CrosNode *n )
{
  if( !tcpIpSocketOpen( &(n->tcpros_listner_proc.socket) ) ||
      !tcpIpSocketSetReuse( &(n->tcpros_listner_proc.socket) ) ||
      !tcpIpSocketSetNonBlocking( &(n->tcpros_listner_proc.socket) ) ||
      !tcpIpSocketBindListen( &(n->tcpros_listner_proc.socket), n->host, 0, CN_MAX_TCPROS_SERVER_CONNECTIONS ) )
  {
    PRINT_ERROR("openTcprosListnerSocket() failed");
    exit( EXIT_FAILURE );
  }
  else
  {
    n->tcpros_port = tcpIpSocketGetPort( &(n->tcpros_listner_proc.socket) );
    PRINT_DEBUG ( "openTcprosListnerSocket() : Accepting tcpros connections at port %d\n", n->tcpros_port );
  }
}

static void handleXmlrpcClientError(CrosNode *node, int i )
{
  XmlrpcProcess *proc = &node->xmlrpc_client_proc[i];
  RosApiCall *call = proc->current_call;

  switch (call->method)
  {
    case CROS_API_REGISTER_SERVICE:
    case CROS_API_UNREGISTER_SERVICE:
    case CROS_API_UNREGISTER_PUBLISHER:
    case CROS_API_UNREGISTER_SUBSCRIBER:
    case CROS_API_REGISTER_PUBLISHER:
    case CROS_API_REGISTER_SUBSCRIBER:
    {
      proc->current_call = NULL;
      enqueueApiCall(&proc->api_calls_queue, call);
      break;
    }
    default:
    {
      // Do nothing
      break;
    }
  }

  tcpIpSocketClose(&proc->socket);
  xmlrpcProcessClear(proc, 1);
  xmlrpcProcessChangeState(proc, XMLRPC_PROCESS_STATE_IDLE);
}

static void handleTcprosClientError( CrosNode *n, int i )
{
  tcpIpSocketClose( &(n->tcpros_client_proc[i].socket) );
  tcprosProcessClear( &(n->tcpros_client_proc[i]), 1 );
  tcprosProcessChangeState( &(n->tcpros_client_proc[i]), TCPROS_PROCESS_STATE_IDLE );
}

static void handleXmlrpcServerError( CrosNode *n, int i )
{
  xmlrpcProcessClear( &(n->xmlrpc_server_proc[i]), 1);
  xmlrpcProcessChangeState( &(n->xmlrpc_server_proc[i]), XMLRPC_PROCESS_STATE_IDLE );
  tcpIpSocketClose( &(n->xmlrpc_server_proc[i].socket) );
}

static void handleTcprosServerError( CrosNode *n, int i )
{
  tcprosProcessClear( &(n->tcpros_server_proc[i]), 1 );
  tcprosProcessChangeState( &(n->tcpros_server_proc[i]), TCPROS_PROCESS_STATE_IDLE );
  tcpIpSocketClose( &(n->tcpros_server_proc[i].socket) );
  n->tcpros_server_proc[i].topic_idx = -1;
}

static void handleRpcrosServerError( CrosNode *n, int i )
{
  tcprosProcessClear( &(n->rpcros_server_proc[i]), 1 );
  tcprosProcessChangeState( &(n->rpcros_server_proc[i]), TCPROS_PROCESS_STATE_IDLE );
  tcpIpSocketClose( &(n->rpcros_server_proc[i].socket) );
}

static void doWithXmlrpcClientSocket( CrosNode *n, int i)
{
  PRINT_VDEBUG ( "doWithXmlrpcClientSocket()\n" );

  XmlrpcProcess *xmlrpc_client_proc = &(n->xmlrpc_client_proc[i]);

  if( xmlrpc_client_proc->state == XMLRPC_PROCESS_STATE_WRITING )
  {
    PRINT_DEBUG ( "doWithXmlrpcClientSocket() : writing\n" );

    if( !xmlrpc_client_proc->socket.connected )
    {

      TcpIpSocketState conn_state = TCPIPSOCKET_FAILED;

      if(i == 0) //connection with roscore node
      {
    	  conn_state = tcpIpSocketConnect( &(xmlrpc_client_proc->socket),
                                       	   n->roscore_host, n->roscore_port );
      }
      else
      {
        int j;

        for(j = 0; j < n->n_subs; j++)
        {
          if(i == n->subs[j].client_xmlrpc_id)
          {
            conn_state = tcpIpSocketConnect( &(xmlrpc_client_proc->socket),
                                             n->subs[j].topic_host, n->subs[j].topic_port );
          }
        }
      }

      if( conn_state == TCPIPSOCKET_IN_PROGRESS )
      {
        PRINT_DEBUG ( "doWithXmlrpcClientSocket() : Wait: connection is established asynchronously\n" );
        // Wait: connection is established asynchronously
        return;
      }
      else if( conn_state == TCPIPSOCKET_FAILED )
      {
        PRINT_ERROR("doWithXmlrpcClientSocket() : Can't connect\n" );
        handleXmlrpcClientError( n, i);
        return;
      }
    }

    cRosApiPrepareRequest( n, i );

    TcpIpSocketState sock_state =  tcpIpSocketWriteString( &(xmlrpc_client_proc->socket), 
                                                           &(xmlrpc_client_proc->message) );
    switch ( sock_state )
    {
      case TCPIPSOCKET_DONE:
        xmlrpcProcessClear(xmlrpc_client_proc, 0);
        xmlrpcProcessChangeState( xmlrpc_client_proc, XMLRPC_PROCESS_STATE_READING );
        break;

      case TCPIPSOCKET_IN_PROGRESS:
        PRINT_DEBUG ( "doWithXmlrpcClientSocket() : Write in progress...\n" );
        break;

      case TCPIPSOCKET_DISCONNECTED:
      case TCPIPSOCKET_FAILED:
      default:
        PRINT_ERROR("doWithXmlrpcClientSocket() : Unexpected failure writing request\n");
        handleXmlrpcClientError( n, i );
        break;
    }
  }
  else if( xmlrpc_client_proc->state == XMLRPC_PROCESS_STATE_READING )
  {
    PRINT_DEBUG ( "doWithXmlrpcClientSocket() : Reading()\n" );
    TcpIpSocketState sock_state = tcpIpSocketReadString( &xmlrpc_client_proc->socket,
                                                         &xmlrpc_client_proc->message );
    //printf("%s\n", xmlrpc_client_proc->message.data);
    XmlrpcParserState parser_state = XMLRPC_PARSER_INCOMPLETE;

    switch ( sock_state )
    {
      case TCPIPSOCKET_DONE:
        parser_state = parseXmlrpcMessage( &xmlrpc_client_proc->message,
                                           &xmlrpc_client_proc->message_type,
                                           NULL,
                                           &xmlrpc_client_proc->response );
        break;

      case TCPIPSOCKET_IN_PROGRESS:
        break;

      case TCPIPSOCKET_DISCONNECTED:
        parser_state = parseXmlrpcMessage( &xmlrpc_client_proc->message,
                                           &xmlrpc_client_proc->message_type,
                                           NULL,
                                           &xmlrpc_client_proc->response );
        xmlrpcProcessChangeState( xmlrpc_client_proc, XMLRPC_PROCESS_STATE_IDLE );
        tcpIpSocketClose ( &xmlrpc_client_proc->socket );
        break;

      case TCPIPSOCKET_FAILED:
      default:
        PRINT_ERROR("doWithXmlrpcClientSocket() : Unexpected failure reading response\n" );
        handleXmlrpcClientError( n, i );
        break;
    }

    switch ( parser_state )
    {
      case XMLRPC_PARSER_DONE:
        PRINT_DEBUG ( "doWithXmlrpcClientSocket() : Done with no error\n" );

        int rc = cRosApiParseResponse( n, i );
        if (rc != 0)
        {
          handleXmlrpcClientError( n, i );
        }
        else
        {
          xmlrpcProcessClear(xmlrpc_client_proc, 1);
          xmlrpcProcessChangeState(xmlrpc_client_proc, XMLRPC_PROCESS_STATE_IDLE );
          tcpIpSocketClose(&xmlrpc_client_proc->socket);
        }
        break;

      case XMLRPC_PARSER_INCOMPLETE:
        break;

      case XMLRPC_PARSER_ERROR:
      default:
        handleXmlrpcClientError( n, i );
        break;
    }
  }
}

static void doWithXmlrpcServerSocket( CrosNode *n, int i )
{
  PRINT_VDEBUG ( "doWithXmlrpcServerSocket()\n" );

  XmlrpcProcess *server_proc = &(n->xmlrpc_server_proc[i]);

  if( server_proc->state == XMLRPC_PROCESS_STATE_READING )
  {
    PRINT_DEBUG ( "doWithXmlrpcServerSocket() : Reading() index %d \n", i );
    TcpIpSocketState sock_state = tcpIpSocketReadString( &(server_proc->socket), 
                                                         &(server_proc->message) );
    XmlrpcParserState parser_state = XMLRPC_PARSER_INCOMPLETE;

    switch ( sock_state )
    {
      case TCPIPSOCKET_DONE:
        parser_state = parseXmlrpcMessage( &server_proc->message,
                                           &server_proc->message_type,
                                           &server_proc->method,
                                           &server_proc->params);
        break;

      case TCPIPSOCKET_IN_PROGRESS:
        break;

      case TCPIPSOCKET_DISCONNECTED:
        xmlrpcProcessClear( &(n->xmlrpc_server_proc[i]), 1);
        xmlrpcProcessChangeState( &(n->xmlrpc_server_proc[i]), XMLRPC_PROCESS_STATE_IDLE );
        tcpIpSocketClose( &(n->xmlrpc_server_proc[i].socket) );
        break;
      case TCPIPSOCKET_FAILED:
      default:
        PRINT_ERROR("doWithXmlrpcServerSocket() : Unexpected failure reading request\n");
        handleXmlrpcServerError( n, i );
        break;
    }

    switch ( parser_state )
    {
      case XMLRPC_PARSER_DONE:

        PRINT_DEBUG ( "doWithXmlrpcServerSocket() : Done read() and parse() with no error\n" );
        int rc = cRosApiParseRequestPrepareResponse( n, i );
        if (rc == -1)
        {
          handleXmlrpcServerError( n, i );
          break;
        }
        xmlrpcProcessChangeState( server_proc, XMLRPC_PROCESS_STATE_WRITING );
        break;
      case XMLRPC_PARSER_INCOMPLETE:
        break;

      case XMLRPC_PARSER_ERROR:
      default:
        handleXmlrpcServerError( n, i );
        break;
    }
  }
  else if( server_proc->state == XMLRPC_PROCESS_STATE_WRITING )
  {
    PRINT_DEBUG ( "doWithXmlrpcServerSocket() : writing() index %d \n", i );
    TcpIpSocketState sock_state =  tcpIpSocketWriteString( &(server_proc->socket), 
                                                           &(server_proc->message) );
    switch ( sock_state )
    {
      case TCPIPSOCKET_DONE:
        xmlrpcProcessClear( server_proc, 1);
        xmlrpcProcessChangeState( server_proc, XMLRPC_PROCESS_STATE_READING );
        break;

      case TCPIPSOCKET_IN_PROGRESS:
        break;

      case TCPIPSOCKET_DISCONNECTED:
        xmlrpcProcessClear( server_proc, 1);
        xmlrpcProcessChangeState( server_proc, XMLRPC_PROCESS_STATE_IDLE );
        tcpIpSocketClose( &(server_proc->socket) );
        break;

      case TCPIPSOCKET_FAILED:
      default:
        PRINT_ERROR("doWithXmlrpcServerSocket() : Unexpected failure writing response\n");
        handleXmlrpcServerError( n, i );
        break;
    }
  }
}

static void doWithTcprosClientSocket( CrosNode *n, int client_idx)
{
  PRINT_VDEBUG ( "doWithTcprosSubscriberNode()\n" );

  TcprosProcess *client_proc = &(n->tcpros_client_proc[client_idx]);

  switch ( client_proc->state )
  {
    case  TCPROS_PROCESS_STATE_CONNECTING:
    {
      SubscriberNode *sub = &(n->subs[client_proc->topic_idx]);
      tcprosProcessClear( client_proc, 0 );
      TcpIpSocketState conn_state = tcpIpSocketConnect( &(client_proc->socket),
                                           sub->topic_host, sub->tcpros_port );
      switch (conn_state)
      {
        case TCPIPSOCKET_DONE:
        {
          tcprosProcessChangeState( client_proc, TCPROS_PROCESS_STATE_WRITING_HEADER );
          break;
        }
        case TCPIPSOCKET_IN_PROGRESS:
        {
          PRINT_DEBUG ( "doWithXmlrpcClientSocket() : Wait: connection is established asynchronously\n" );
          // Wait: connection is established asynchronously
          break;
        }
        case TCPIPSOCKET_FAILED:
        {
          PRINT_DEBUG ( "doWithXmlrpcClientSocket() : error\n" );
          handleTcprosClientError( n, client_idx);
          break;
        }
        default:
        {
          assert(0);
          break;
        }
      }

      break;
    }
    case TCPROS_PROCESS_STATE_WRITING_HEADER:
    {
      cRosMessagePrepareSubcriptionHeader( n, client_idx);

      TcpIpSocketState sock_state =  tcpIpSocketWriteBuffer( &(client_proc->socket),
                                                           &(client_proc->packet) );

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          tcprosProcessClear( client_proc, 0);
          client_proc->left_to_recv = sizeof(uint32_t);
          tcprosProcessChangeState( client_proc, TCPROS_PROCESS_STATE_READING_HEADER_SIZE );

          break;

        case TCPIPSOCKET_IN_PROGRESS:
          break;

        case TCPIPSOCKET_DISCONNECTED:
          tcprosProcessClear( client_proc, 1);
          tcprosProcessChangeState( client_proc, TCPROS_PROCESS_STATE_IDLE );
          tcpIpSocketClose( &(client_proc->socket) );
          break;

        case TCPIPSOCKET_FAILED:
        default:
          handleTcprosClientError( n, client_idx );
          break;
      }
      break;
    }
    case TCPROS_PROCESS_STATE_READING_HEADER_SIZE:
    {
      size_t n_reads;
      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(client_proc->socket),
                                                          &(client_proc->packet),
                                                          client_proc->left_to_recv,
                                                          &n_reads);

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          client_proc->left_to_recv -= n_reads;
          if (client_proc->left_to_recv == 0)
          {
            const unsigned char *data = dynBufferGetCurrentData(&client_proc->packet);
            uint32_t header_size;
            ROS_TO_HOST_UINT32(*((uint32_t *)data), header_size);
            tcprosProcessClear( client_proc, 0 );
            client_proc->left_to_recv = header_size;
            tcprosProcessChangeState( client_proc, TCPROS_PROCESS_STATE_READING_HEADER);
            goto read_header;
          }
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          break;
        case TCPIPSOCKET_DISCONNECTED:
        case TCPIPSOCKET_FAILED:
        default:
          handleTcprosClientError( n, client_idx );
          break;
      }

      break;
    }
    read_header:
    case TCPROS_PROCESS_STATE_READING_HEADER:
    {
      size_t n_reads;
      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(client_proc->socket),
                                                          &(client_proc->packet),
                                                          client_proc->left_to_recv,
                                                          &n_reads);
      TcprosParserState parser_state = TCPROS_PARSER_ERROR;

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          client_proc->left_to_recv -= n_reads;
          if (client_proc->left_to_recv == 0)
          {
            parser_state = cRosMessageParsePublicationHeader( n, client_idx );
            break;
          }
          parser_state = TCPROS_PARSER_HEADER_INCOMPLETE;
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          parser_state = TCPROS_PARSER_HEADER_INCOMPLETE;
          break;
        case TCPIPSOCKET_DISCONNECTED:
        case TCPIPSOCKET_FAILED:
        default:
          handleTcprosClientError( n, client_idx );
          break;
      }

      switch ( parser_state )
      {
        case TCPROS_PARSER_DONE:
          tcprosProcessClear( client_proc, 0);
          client_proc->left_to_recv = sizeof(uint32_t);
          tcprosProcessChangeState( client_proc, TCPROS_PROCESS_STATE_READING_SIZE );
          break;
        case TCPROS_PARSER_HEADER_INCOMPLETE:
          break;
        case TCPROS_PARSER_ERROR:
        default:
          handleTcprosClientError( n, client_idx );
          break;
      }
    }
    case TCPROS_PROCESS_STATE_READING_SIZE:
    {
      size_t n_reads;
      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(client_proc->socket),
                                                          &(client_proc->packet),
                                                          client_proc->left_to_recv,
                                                          &n_reads);

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          client_proc->left_to_recv -= n_reads;
          if (client_proc->left_to_recv == 0)
          {
            const unsigned char *data = dynBufferGetCurrentData(&client_proc->packet);
            uint32_t msg_size = (uint32_t)*data;
            tcprosProcessClear( client_proc, 0);
            client_proc->left_to_recv = msg_size;
            tcprosProcessChangeState( client_proc, TCPROS_PROCESS_STATE_READING);
            goto read_msg;
          }
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          break;
        case TCPIPSOCKET_DISCONNECTED:
        case TCPIPSOCKET_FAILED:
        default:
          handleTcprosClientError( n, client_idx );
          break;
      }
      break;
    }
    read_msg:
    case TCPROS_PROCESS_STATE_READING:
    {
      size_t n_reads;
      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(client_proc->socket),
                                                          &(client_proc->packet),
                                                          client_proc->left_to_recv,
                                                          &n_reads);

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          client_proc->left_to_recv -= n_reads;
          if (client_proc->left_to_recv == 0)
          {
              cRosMessageParsePublicationPacket(n, client_idx);
              tcprosProcessClear( client_proc, 0);
              client_proc->left_to_recv = sizeof(uint32_t);
              tcprosProcessChangeState( client_proc, TCPROS_PROCESS_STATE_READING_SIZE );
          }
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          break;
        case TCPIPSOCKET_DISCONNECTED:
        case TCPIPSOCKET_FAILED:
        default:
          handleTcprosClientError( n, client_idx );
          break;
      }
      break;
    }
    default:
    {
      // Invalid flow
      assert(0);
    }

  }
}

static void doWithTcprosServerSocket( CrosNode *n, int i )
{
  PRINT_VDEBUG ( "doWithTcprosServerSocket()\n" );
  
  TcprosProcess *server_proc = &(n->tcpros_server_proc[i]);

  if( server_proc->state == TCPROS_PROCESS_STATE_READING_HEADER)
  {
    PRINT_DEBUG ( "doWithTcprosServerSocket() : Reading header index %d \n", i );
    tcprosProcessClear( server_proc, 0);
    TcpIpSocketState sock_state = tcpIpSocketReadBuffer( &(server_proc->socket), 
                                                         &(server_proc->packet) );
    TcprosParserState parser_state = TCPROS_PARSER_ERROR;
    
    switch ( sock_state )
    {
      case TCPIPSOCKET_DONE:
        parser_state = cRosMessageParseSubcriptionHeader( n, i );
        break;
        
      case TCPIPSOCKET_IN_PROGRESS:
      	parser_state = TCPROS_PARSER_HEADER_INCOMPLETE;
        break;
        
      case TCPIPSOCKET_DISCONNECTED:
      case TCPIPSOCKET_FAILED:
      default:
        handleTcprosServerError( n, i );
        break;
    }
    
    switch ( parser_state )
    {
      case TCPROS_PARSER_DONE:
                               
        PRINT_DEBUG ( "doWithTcprosServerSocket() : Done read() and parse() with no error\n" );
        tcprosProcessClear( server_proc, 0);
        cRosMessagePreparePublicationHeader( n, i );
        tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_WRITING );
        break;
      case TCPROS_PARSER_HEADER_INCOMPLETE:
        break;
        
      case TCPROS_PARSER_ERROR:
      default:
        handleTcprosServerError( n, i );
        break;
    }
  }
  else if( server_proc->state == TCPROS_PROCESS_STATE_START_WRITING ||
           server_proc->state == TCPROS_PROCESS_STATE_WRITING )
  {
    PRINT_DEBUG ( "doWithTcprosServerSocket() : writing() index %d \n", i );
    if( server_proc->state == TCPROS_PROCESS_STATE_START_WRITING )
    {
      tcprosProcessClear( server_proc, 0 );
      cRosMessagePreparePublicationPacket( n, i );
      tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_WRITING );      
    }
    TcpIpSocketState sock_state =  tcpIpSocketWriteBuffer( &(server_proc->socket), 
                                                           &(server_proc->packet) );
    
    switch ( sock_state )
    {
      case TCPIPSOCKET_DONE:
        PRINT_DEBUG ( "doWithTcprosServerSocket() : Done write() with no error\n" );
        tcprosProcessClear( server_proc, 0);
        tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_WAIT_FOR_WRITING );
        break;
        
      case TCPIPSOCKET_IN_PROGRESS:
        break;

      case TCPIPSOCKET_DISCONNECTED:
        tcprosProcessClear( server_proc, 1);
        tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_IDLE );
        tcpIpSocketClose( &(server_proc->socket) );
        break;
        
      case TCPIPSOCKET_FAILED:
      default:
        handleTcprosServerError( n, i );
        break;
    }
  }  
}

static void doWithRpcrosServerSocket(CrosNode *n, int i)
{
  PRINT_VDEBUG ( "doWithRpcrosServerSocket()\n" );

  TcprosProcess *server_proc = &(n->rpcros_server_proc[i]);

  switch (server_proc->state)
  {
    case TCPROS_PROCESS_STATE_READING_HEADER_SIZE:
    {
      size_t n_reads;
      if (server_proc->left_to_recv == 0)
        server_proc->left_to_recv = sizeof(uint32_t);

      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(server_proc->socket),
                                                          &(server_proc->packet),
                                                          server_proc->left_to_recv,
                                                          &n_reads);

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          server_proc->left_to_recv -= n_reads;
          if (server_proc->left_to_recv == 0)
          {
            const unsigned char *data = dynBufferGetCurrentData(&server_proc->packet);
            uint32_t header_size;
            ROS_TO_HOST_UINT32(*((uint32_t *)data), header_size);
            tcprosProcessClear( server_proc, 0 );
            server_proc->left_to_recv = header_size;
            tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_READING_HEADER);
            goto read_header;
          }
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          break;
        case TCPIPSOCKET_DISCONNECTED:
        case TCPIPSOCKET_FAILED:
        default:
          handleRpcrosServerError( n, i );
          break;
      }

      break;
    }
    read_header:
    case TCPROS_PROCESS_STATE_READING_HEADER:
    {
      size_t n_reads;
      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(server_proc->socket),
                                                          &(server_proc->packet),
                                                          server_proc->left_to_recv,
                                                          &n_reads);
      TcprosParserState parser_state = TCPROS_PARSER_ERROR;

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          server_proc->left_to_recv -= n_reads;
          if (server_proc->left_to_recv == 0)
          {
            parser_state = cRosMessageParseServiceCallerHeader( n, i );
            break;
          }
          parser_state = TCPROS_PARSER_HEADER_INCOMPLETE;
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          parser_state = TCPROS_PARSER_HEADER_INCOMPLETE;
          break;
        case TCPIPSOCKET_DISCONNECTED:
        case TCPIPSOCKET_FAILED:
        default:
          handleRpcrosServerError( n, i );
          break;
      }

      switch ( parser_state )
      {
        case TCPROS_PARSER_DONE:
          tcprosProcessClear( server_proc, 0);
          tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_WRITING_HEADER );
          break;
        case TCPROS_PARSER_HEADER_INCOMPLETE:
          break;
        case TCPROS_PARSER_ERROR:
        default:
          handleRpcrosServerError( n, i );
          break;
      }

      break;
    }
    case TCPROS_PROCESS_STATE_WRITING_HEADER:
    {
      PRINT_DEBUG ( "doWithRpcrosServerSocket() : writing() index %d \n", i );

      cRosMessagePrepareServiceProviderHeader( n, i );

      TcpIpSocketState sock_state =  tcpIpSocketWriteBuffer( &(server_proc->socket),
                                                            &(server_proc->packet) );

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          PRINT_DEBUG ( "doWithRpcrosServerSocket() : Done write() with no error\n" );
          if(server_proc->probe)
          {
            tcprosProcessClear( server_proc, 1 );
            tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_IDLE );
          }
          else
          {
            tcprosProcessClear( server_proc, 0 );
            server_proc->left_to_recv = sizeof(uint32_t);
            tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_READING_SIZE );
          }
          break;

        case TCPIPSOCKET_IN_PROGRESS:
          break;

        case TCPIPSOCKET_DISCONNECTED:
          tcprosProcessClear( server_proc , 1);
          tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_IDLE );
          tcpIpSocketClose( &(server_proc->socket) );
          break;

        case TCPIPSOCKET_FAILED:
        default:
          handleRpcrosServerError( n, i );
          tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_IDLE );
          break;
      }

      break;
    }
    case TCPROS_PROCESS_STATE_READING_SIZE:
    {
      size_t n_reads;
      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(server_proc->socket),
                                                          &(server_proc->packet),
                                                          server_proc->left_to_recv,
                                                          &n_reads);

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          server_proc->left_to_recv -= n_reads;
          if (server_proc->left_to_recv == 0)
          {
            const unsigned char *data = dynBufferGetCurrentData(&server_proc->packet);
            uint32_t msg_size = (uint32_t)*data;
            tcprosProcessClear( server_proc, 0);
            if (msg_size == 0)
            {
              PRINT_DEBUG ( "doWithRpcrosServerSocket() : Done read() with no error\n" );
              cRosMessagePrepareServiceResponsePacket(n, i);
              tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_WRITING);
              goto write_msg;
            }
            else
            {
              server_proc->left_to_recv = msg_size;
              tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_READING);
              goto read_msg;
            }
          }
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          break;
        case TCPIPSOCKET_DISCONNECTED:
        case TCPIPSOCKET_FAILED:
        default:
          handleRpcrosServerError( n, i );
          break;
      }
      break;
    }
    read_msg:
    case TCPROS_PROCESS_STATE_READING:
    {
      PRINT_DEBUG ( "doWithRpcrosServerSocket() : reading() index %d \n", i );

      size_t n_reads;
      TcpIpSocketState sock_state = tcpIpSocketReadBufferEx( &(server_proc->socket),
                                                          &(server_proc->packet),
                                                          server_proc->left_to_recv,
                                                          &n_reads);

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          server_proc->left_to_recv -= n_reads;
          if (server_proc->left_to_recv == 0)
          {
              PRINT_DEBUG ( "doWithRpcrosServerSocket() : Done read() with no error\n" );
              cRosMessagePrepareServiceResponsePacket(n, i);
              tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_WRITING );
          }
          break;
        case TCPIPSOCKET_IN_PROGRESS:
          break;
        case TCPIPSOCKET_DISCONNECTED:
          tcprosProcessClear( server_proc, 1 );
          tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_IDLE );
          tcpIpSocketClose( &(server_proc->socket) );
          break;
        case TCPIPSOCKET_FAILED:
        default:
          handleRpcrosServerError( n, i );
          break;
      }

      break;
    }
    write_msg:
    case TCPROS_PROCESS_STATE_WRITING:
    {
      PRINT_DEBUG ( "doWithRpcrosServerSocket() : writing() index %d \n", i );

      TcpIpSocketState sock_state =  tcpIpSocketWriteBuffer( &(server_proc->socket),
                                                            &(server_proc->packet) );

      switch ( sock_state )
      {
        case TCPIPSOCKET_DONE:
          PRINT_DEBUG ( "doWithRpcrosServerSocket() : Done write() with no error\n" );
          tcprosProcessClear( server_proc, 1 );
          tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_IDLE);
          break;

        case TCPIPSOCKET_IN_PROGRESS:
          break;

        case TCPIPSOCKET_DISCONNECTED:
          tcprosProcessClear( server_proc, 1 );
          tcprosProcessChangeState( server_proc, TCPROS_PROCESS_STATE_IDLE );
          tcpIpSocketClose( &(server_proc->socket) );
          break;

        case TCPIPSOCKET_FAILED:
        default:
          handleRpcrosServerError( n, i );
          break;
      }

      break;
    }
    default:
    {
      // Invalid flow
      assert(0);
    }
  }
}

CrosNode *cRosNodeCreate ( char* node_name, char *node_host, char *roscore_host,
                           unsigned short roscore_port, uint64_t const *select_timeout_ms )
{
  PRINT_VDEBUG ( "cRosNodeCreate()\n" );

  signal(SIGPIPE, SIG_IGN);
  
  if(node_name == NULL || node_host == NULL || roscore_host == NULL )
  {
    PRINT_ERROR ( "cRosNodeCreate() : NULL parameters\n" );
    return NULL;
  }
  
  CrosNode *new_n = ( CrosNode * ) malloc ( sizeof ( CrosNode ) );

  if ( new_n == NULL )
  {
    PRINT_ERROR ( "cRosNodeCreate() : Can't allocate memory\n" );
    return NULL;
  }

  new_n->name = new_n->host = new_n->roscore_host = NULL;

  new_n->name = ( char * ) malloc ( ( strlen ( node_name ) + 1 ) *sizeof ( char ) );
  new_n->host = ( char * ) malloc ( ( strlen ( node_host ) + 1 ) *sizeof ( char ) );
  new_n->roscore_host = ( char * ) malloc ( ( strlen ( roscore_host ) + 1 ) *sizeof ( char ) );

  if ( new_n->name == NULL || new_n->host == NULL || new_n->roscore_host == NULL )
  {
    PRINT_ERROR ( "cRosNodeCreate() : Can't allocate memory\n" );
    cRosNodeDestroy ( new_n );
    return NULL;
  }

  strcpy ( new_n->name, node_name );
  strcpy ( new_n->host, node_host );
  strcpy ( new_n->roscore_host, roscore_host );
  
  new_n->xmlrpc_port = 0;
  new_n->tcpros_port = 0;
  new_n->roscore_port = roscore_port;
  new_n->roscore_pid = -1;

  xmlrpcProcessInit( &(new_n->xmlrpc_listner_proc) );

  int i;

  for (i = 0 ; i < CN_MAX_XMLRPC_SERVER_CONNECTIONS; i++)
    xmlrpcProcessInit( &(new_n->xmlrpc_server_proc[i]) ); 
  
  for ( i = 0 ; i < CN_MAX_XMLRPC_CLIENT_CONNECTIONS; i++)
  	xmlrpcProcessInit( &(new_n->xmlrpc_client_proc[i]) );

  tcprosProcessInit( &(new_n->tcpros_listner_proc) );

  for ( i = 0; i < CN_MAX_TCPROS_SERVER_CONNECTIONS; i++)
    tcprosProcessInit( &(new_n->tcpros_server_proc[i]) );

  for ( i = 0; i < CN_MAX_TCPROS_CLIENT_CONNECTIONS; i++)
    tcprosProcessInit( &(new_n->tcpros_client_proc[i]) );

  tcprosProcessInit( &(new_n->rpcros_listner_proc) );

  for ( i = 0; i < CN_MAX_RPCROS_SERVER_CONNECTIONS; i++)
    tcprosProcessInit( &(new_n->rpcros_server_proc[i]) );

  for ( i = 0; i < CN_MAX_PUBLISHED_TOPICS; i++)
  {
    new_n->pubs[i].message_definition = NULL;
    new_n->pubs[i].topic_name = NULL;
    new_n->pubs[i].topic_type = NULL;
    new_n->pubs[i].md5sum = NULL;
    new_n->pubs[i].callback = NULL;
    new_n->pubs[i].context = NULL;
    new_n->pubs[i].loop_period = 1000;
  }

  new_n->n_pubs = 0;

  for ( i = 0; i < CN_MAX_SUBSCRIBED_TOPICS; i++)
  {
    new_n->subs[i].message_definition = NULL;
    new_n->subs[i].topic_host = NULL;
    new_n->subs[i].topic_port = -1;
    new_n->subs[i].topic_name = NULL;
    new_n->subs[i].topic_type = NULL;
    new_n->subs[i].md5sum = NULL;
    new_n->subs[i].callback = NULL;
    new_n->subs[i].context = NULL;
    new_n->subs[i].client_xmlrpc_id = -1;
    new_n->subs[i].client_tcpros_id = -1;
    new_n->subs[i].tcpros_port = -1;
  }

  new_n->n_subs = 0;

  for ( i = 0; i < CN_MAX_SERVICE_PROVIDERS; i++)
  {
    new_n->services[i].service_name = NULL;
    new_n->services[i].service_type = NULL;
    new_n->services[i].md5sum = NULL;
    new_n->services[i].callback = NULL;
    new_n->services[i].context = NULL;
    new_n->services[i].servicerequest_type = NULL;
    new_n->services[i].serviceresponse_type = NULL;
  }

  new_n->n_services = 0;

  if (select_timeout_ms == NULL)
    new_n->select_timeout = UINT64_MAX;
  else
    new_n->select_timeout = *select_timeout_ms;
  new_n->pid = (int)getpid();
  
  for(i = 0; i < CN_MAX_XMLRPC_CLIENT_CONNECTIONS; i++)
  {
    openXmlrpcClientSocket( new_n, i );
  }

  for(i = 0; i < CN_MAX_TCPROS_CLIENT_CONNECTIONS; i++)
  {
    openTcprosClientSocket( new_n, i );
  }

  openXmlrpcListnerSocket( new_n );
  openTcprosListnerSocket( new_n );
  openRpcrosListnerSocket( new_n );

  return new_n;
}

void cRosNodeDestroy ( CrosNode *n )
{
  PRINT_VDEBUG ( "cRosNodeDestroy()\n" );

  if ( n == NULL )
    return;

  xmlrpcProcessRelease( &(n->xmlrpc_listner_proc) );

  int i;

  for (i = 0; i < CN_MAX_XMLRPC_SERVER_CONNECTIONS; i++)
    xmlrpcProcessRelease( &(n->xmlrpc_server_proc[i]) ); 

  for(i = 0; i < CN_MAX_XMLRPC_CLIENT_CONNECTIONS; i++)
    xmlrpcProcessRelease( &(n->xmlrpc_client_proc[i]) );

  tcprosProcessRelease( &(n->tcpros_listner_proc) );

  for ( i = 0; i < CN_MAX_TCPROS_SERVER_CONNECTIONS; i++)
    tcprosProcessRelease( &(n->tcpros_server_proc[i]) );

  for ( i = 0; i < CN_MAX_TCPROS_CLIENT_CONNECTIONS; i++)
    tcprosProcessRelease( &(n->tcpros_server_proc[i]) ); 

  for ( i = 0; i < CN_MAX_RPCROS_SERVER_CONNECTIONS; i++)
    tcprosProcessRelease( &(n->rpcros_server_proc[i]) );

  if ( n->name != NULL ) free ( n->name );
  if ( n->host != NULL ) free ( n->host );
  if ( n->roscore_host != NULL ) free ( n->roscore_host );

  for ( i = 0; i < CN_MAX_PUBLISHED_TOPICS; i++)
  {  
    if ( n->pubs[i].message_definition != NULL ) free ( n->pubs[i].message_definition );
    if ( n->pubs[i].topic_name != NULL ) free ( n->pubs[i].topic_name );
    if ( n->pubs[i].topic_type != NULL ) free ( n->pubs[i].topic_type );
    if ( n->pubs[i].md5sum != NULL ) free ( n->pubs[i].md5sum );
  }

  for ( i = 0; i < CN_MAX_SUBSCRIBED_TOPICS; i++)
  {
    if ( n->subs[i].message_definition != NULL ) free ( n->subs[i].message_definition );
    if ( n->subs[i].topic_name != NULL ) free ( n->subs[i].topic_name );
    if ( n->subs[i].topic_type != NULL ) free ( n->subs[i].topic_type );
    if ( n->subs[i].md5sum != NULL ) free ( n->subs[i].md5sum );
    if ( n->subs[i].topic_host != NULL ) free ( n->subs[i].topic_host );
  }

  for ( i = 0; i < CN_MAX_SERVICE_PROVIDERS; i++)
  {
    if ( n->services[i].service_name != NULL ) free ( n->services[i].service_name );
    if ( n->services[i].service_type != NULL ) free ( n->services[i].service_type );
    if ( n->services[i].servicerequest_type != NULL ) free ( n->services[i].service_type );
    if ( n->services[i].serviceresponse_type != NULL ) free ( n->services[i].service_type );
    if ( n->services[i].md5sum != NULL ) free ( n->services[i].md5sum );
  }
}

int cRosNodeRegisterPublisher ( CrosNode *n, char *message_definition, 
                                char *topic_name, char *topic_type, char *md5sum, int loop_period,
                                PublisherCallback callback, void *data_context )
{
  PRINT_VDEBUG ( "cRosNodeRegisterPublisher()\n" );
  PRINT_INFO ( "Publishing topic %s type %s \n", topic_name, topic_type );

  if ( n->n_pubs >= CN_MAX_PUBLISHED_TOPICS )
  {
    PRINT_ERROR ( "cRosNodeRegisterPublisher() : Can't register a new publisher: \
                 reached the maximum number of published topics\n");
    return -1;
  }

  char *pub_message_definition = ( char * ) malloc ( ( strlen ( message_definition ) + 1 ) * sizeof ( char ) );
  char *pub_topic_name = ( char * ) malloc ( ( strlen ( topic_name ) + 1 ) * sizeof ( char ) );
  char *pub_topic_type = ( char * ) malloc ( ( strlen ( topic_type ) + 1 ) * sizeof ( char ) );
  char *pub_md5sum = ( char * ) malloc ( ( strlen ( md5sum ) + 1 ) * sizeof ( char ) );

  if ( pub_message_definition == NULL || pub_topic_name == NULL || 
       pub_topic_type == NULL || pub_md5sum == NULL)
  {
    PRINT_ERROR ( "cRosNodeRegisterPublisher() : Can't allocate memory\n" );
    return -1;
  }

  strcpy ( pub_message_definition, message_definition );
  strcpy ( pub_topic_name, topic_name );
  strcpy ( pub_topic_type, topic_type );
  strcpy ( pub_md5sum, md5sum );

  int pubidx = n->n_pubs;
  PublisherNode *node = &n->pubs[pubidx];
  node->message_definition = pub_message_definition;
  node->topic_name = pub_topic_name;
  node->topic_type = pub_topic_type;
  node->md5sum = pub_md5sum;

  node->loop_period = loop_period;
  node->callback = callback;
  node->context = data_context;

  n->n_pubs++;

  int rc = enqueuePubliserAdvertise(n, pubidx);
  if (rc == -1)
    return -1;

  return pubidx;
}

int cRosNodeRegisterServiceProvider( CrosNode *n, char *service_name,
                               char *service_type, char *md5sum,
                                ServiceProviderCallback callback, void *data_context)
{
  PRINT_VDEBUG ( "cRosNodeRegisterServiceProvider()\n" );
  PRINT_INFO ( "Registering service %s type %s \n", service_name, service_type );

  if ( n->n_services >= CN_MAX_SERVICE_PROVIDERS )
  {
    PRINT_ERROR ( "cRosNodeRegisterPublisher() : Can't register a new service provider: \
                 reached the maximum number of services\n");
    return -1;
  }

  char *srv_service_name = ( char * ) malloc ( ( strlen ( service_name ) + 1 ) * sizeof ( char ) );
  char *srv_service_type = ( char * ) malloc ( ( strlen ( service_type ) + 1 ) * sizeof ( char ) );
  char *srv_servicerequest_type = ( char * ) malloc ( ( strlen ( service_type ) + strlen("Request") + 1 ) * sizeof ( char ) );
  char *srv_serviceresponse_type = ( char * ) malloc ( ( strlen ( service_type ) + strlen("Response") + 1 ) * sizeof ( char ) );
  char *srv_md5sum = ( char * ) malloc ( ( strlen ( md5sum ) + 1 ) * sizeof ( char ) );

  if ( srv_service_name == NULL || srv_service_type == NULL ||
       srv_md5sum == NULL)
  {
    PRINT_ERROR ( "cRosNodeRegisterServiceProvider() : Can't allocate memory\n" );
    return -1;
  }

  strncpy ( srv_service_name, service_name, strlen ( service_name ) + 1 );
  strncpy ( srv_service_type, service_type, strlen ( service_type ) + 1 );
  strncpy ( srv_servicerequest_type, service_type, strlen ( service_type ) + 1 );
  strncat ( srv_servicerequest_type, "Request", strlen("Request") + 1 );
  strncpy ( srv_serviceresponse_type, service_type, strlen ( service_type ) + 1 );
  strncat ( srv_serviceresponse_type, "Response", strlen("Response") +1 );
  strncpy ( srv_md5sum, md5sum, strlen(md5sum) + 1 );

  int serviceidx = n->n_services;
  ServiceProviderNode *node = &(n->services[serviceidx]);

  node->service_name = srv_service_name;
  node->service_type = srv_service_type;
  node->servicerequest_type = srv_servicerequest_type;
  node->serviceresponse_type = srv_serviceresponse_type;
  node->md5sum = srv_md5sum;
  node->callback = callback;
  node->context = data_context;

  n->n_services++;

  int rc = enqueueServiceAdvertise(n, serviceidx);
  if (rc == -1)
    return -1;

  return serviceidx;
}

int cRosNodeRegisterSubscriber(CrosNode *n, char *message_definition,
                               char *topic_name, char *topic_type, char *md5sum,
                               SubscriberCallback callback, void *data_context)
{
  PRINT_VDEBUG ( "cRosNodeRegisterSubscriber()\n" );
  PRINT_INFO ( "Subscribing to topic %s type %s \n", topic_name, topic_type );

  if ( n->n_subs >= CN_MAX_SUBSCRIBED_TOPICS )
  {
    PRINT_ERROR ( "cRosNodeRegisterSubscriber() : Can't register a new subscriber: \
                  reached the maximum number of published topics\n");
    return -1;
  }

  char *pub_message_definition = ( char * ) malloc ( ( strlen ( message_definition ) + 1 ) * sizeof ( char ) );
  char *pub_topic_name = ( char * ) malloc ( ( strlen ( topic_name ) + 1 ) * sizeof ( char ) );
  char *pub_topic_type = ( char * ) malloc ( ( strlen ( topic_type ) + 1 ) * sizeof ( char ) );
  char *pub_md5sum = ( char * ) malloc ( ( strlen ( md5sum ) + 1 ) * sizeof ( char ) );

  if ( pub_topic_name == NULL || pub_topic_type == NULL )
  {
    PRINT_ERROR ( "cRosNodeRegisterSubscriber() : Can't allocate memory\n" );
    return -1;
  }

  strcpy ( pub_message_definition, message_definition );
  strcpy ( pub_topic_name, topic_name );
  strcpy ( pub_topic_type, topic_type );
  strcpy ( pub_md5sum, md5sum );

  int subidx = n->n_subs;
  SubscriberNode *sub = &n->subs[subidx];
  sub->message_definition = pub_message_definition;
  sub->topic_name = pub_topic_name;
  sub->topic_type = pub_topic_type;
  sub->md5sum = pub_md5sum;

  sub->callback = callback;
  sub->context = data_context;

  int clientidx = subidx + 1;
  sub->client_xmlrpc_id = clientidx;
  sub->client_tcpros_id = clientidx;

  TcprosProcess *client_proc = &(n->tcpros_client_proc[clientidx]);
  client_proc->topic_idx = subidx;

  n->n_subs++;

  int rc = enqueueSubscriberAdvertise(n, subidx);
  if (rc == -1)
    return -1;

  return subidx;
}

int unregisterSubscriber(CrosNode *node, int subidx)
{
  return 0;
}

int unregisterPublisher(CrosNode *node, int pubidx)
{
  return 0;
}

int unregisterService(CrosNode *node, int serviceidx)
{
  return 0;
}

void cRosNodeDoEventsLoop ( CrosNode *n )
{
  PRINT_VDEBUG ( "cRosNodeDoEventsLoop ()\n" );
  
  int nfds = -1;
  fd_set r_fds, w_fds, err_fds;
  int i = 0;
  
  FD_ZERO( &r_fds );
  FD_ZERO( &w_fds );
  FD_ZERO( &err_fds );
  
  int xmlrpc_listner_fd = tcpIpSocketGetFD( &(n->xmlrpc_listner_proc.socket) );
  int tcpros_listner_fd = tcpIpSocketGetFD( &(n->tcpros_listner_proc.socket) );
  int rpcros_listner_fd = tcpIpSocketGetFD( &(n->rpcros_listner_proc.socket) );
  
  /*
   *
   * XMLRPC PROCESSES SELECT() MANAGEMENT
   *
   */

  /* If active (not idle state), add to the select() the XMLRPC clients */
  int next_xmlrpc_client_i = -1;
  for(i = 0; i < CN_MAX_XMLRPC_CLIENT_CONNECTIONS; i++)
  {
    int xmlrpc_client_fd = tcpIpSocketGetFD( &(n->xmlrpc_client_proc[i].socket) );
    fd_set *fdset = NULL;
    if( n->xmlrpc_client_proc[i].state == XMLRPC_PROCESS_STATE_WRITING )
    {
      fdset = &w_fds;
      if( xmlrpc_client_fd > nfds ) nfds = xmlrpc_client_fd;
    }
    else if( n->xmlrpc_client_proc[i].state == XMLRPC_PROCESS_STATE_READING )
    {
      fdset = &r_fds;
      if( xmlrpc_client_fd > nfds ) nfds = xmlrpc_client_fd;
    }

    if (fdset != NULL)
    {
      if( !n->xmlrpc_client_proc[0].socket.open )
        openXmlrpcClientSocket( n, 0 );

      FD_SET( xmlrpc_client_fd, fdset);
      FD_SET( xmlrpc_client_fd, &err_fds);
    }
  }

  //printf("FD_SET COUNT. R: %d W: %d\n", r_count, w_count);

  /* Add to the select() the active XMLRPC servers */
  int next_xmlrpc_server_i = -1;
  for( i = 0; i < CN_MAX_XMLRPC_SERVER_CONNECTIONS; i++ )
  {
    int server_fd = tcpIpSocketGetFD( &(n->xmlrpc_server_proc[i].socket) );
    
    if( next_xmlrpc_server_i < 0 &&
        n->xmlrpc_server_proc[i].state == XMLRPC_PROCESS_STATE_IDLE )
    {
      next_xmlrpc_server_i = i;
    }
    else if( n->xmlrpc_server_proc[i].state == XMLRPC_PROCESS_STATE_READING )
    {
      FD_SET( server_fd, &r_fds);
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
    else if( n->xmlrpc_server_proc[i].state == XMLRPC_PROCESS_STATE_WRITING )
    {
      
      FD_SET( server_fd, &w_fds);
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
  }
  
  /* If one XMLRPC server is active at least, add to the select() the listener socket */
  if( next_xmlrpc_server_i >= 0)
  {
    FD_SET( xmlrpc_listner_fd, &r_fds);
    FD_SET( xmlrpc_listner_fd, &err_fds);
    if( xmlrpc_listner_fd > nfds ) nfds = xmlrpc_listner_fd;
  }

  /*
   *
   * TCPROS PROCESSES SELECT() MANAGEMENT
   *
   */

  /* If active (not idle state), add to the select() the TCPROS clients */
  int next_tcpros_client_i = -1;
  for(i = 0; i < CN_MAX_TCPROS_CLIENT_CONNECTIONS; i++)
  {
    int tcpros_client_fd = tcpIpSocketGetFD( &(n->tcpros_client_proc[i].socket) );

    if( next_tcpros_client_i < 0 &&
        i != 0 && //the zero-index is reserved to the roscore communications
        n->tcpros_client_proc[i].state == TCPROS_PROCESS_STATE_IDLE )
    {
      next_tcpros_client_i = i;
    }
    else if(n->tcpros_client_proc[i].state == TCPROS_PROCESS_STATE_WRITING_HEADER)
    {
      FD_SET( tcpros_client_fd, &w_fds);
      FD_SET( tcpros_client_fd, &err_fds);
      if( tcpros_client_fd > nfds ) nfds = tcpros_client_fd;
    }
    else if(n->tcpros_client_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER_SIZE ||
            n->tcpros_client_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER ||
            n->tcpros_client_proc[i].state == TCPROS_PROCESS_STATE_READING_SIZE ||
            n->tcpros_client_proc[i].state == TCPROS_PROCESS_STATE_READING)
    {
      FD_SET( tcpros_client_fd, &r_fds);
      FD_SET( tcpros_client_fd, &err_fds);
      if( tcpros_client_fd > nfds ) nfds = tcpros_client_fd;
    }
  }

  /* Add to the select() the active TCPROS servers */
  int next_tcpros_server_i = -1;  
  for( i = 0; i < CN_MAX_TCPROS_SERVER_CONNECTIONS; i++ )
  {
    int server_fd = tcpIpSocketGetFD( &(n->tcpros_server_proc[i].socket) );
    
    if( next_tcpros_server_i < 0 &&
        n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_IDLE )
    {
      next_tcpros_server_i = i;
    }
    else if( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER )
    {
      FD_SET( server_fd, &r_fds);
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
    else if( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_START_WRITING ||
             n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_WRITING )
    {
      FD_SET( server_fd, &w_fds);
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
    else if( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_WAIT_FOR_WRITING )
    {
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
  }
  
  /* If one TCPROS server is available at least, add to the select() the listner socket */
  if( next_tcpros_server_i >= 0)
  {
    FD_SET( tcpros_listner_fd, &r_fds);
    FD_SET( tcpros_listner_fd, &err_fds);
    if( tcpros_listner_fd > nfds ) nfds = tcpros_listner_fd;
  }

  uint64_t timeout = n->select_timeout;
  uint64_t tmp_timeout, cur_time = cRosClockGetTimeMs();

  if( n->xmlrpc_client_proc[0].wake_up_time_ms > cur_time )
    tmp_timeout = n->xmlrpc_client_proc[0].wake_up_time_ms - cur_time;
  else
    tmp_timeout = 0;
  
  if( tmp_timeout < timeout )
    timeout = tmp_timeout;

  for( i = 0; i < CN_MAX_TCPROS_SERVER_CONNECTIONS; i++ )
  {
    if( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_WAIT_FOR_WRITING)
    {
      if( n->tcpros_server_proc[i].wake_up_time_ms > cur_time )
        tmp_timeout = n->tcpros_server_proc[i].wake_up_time_ms - cur_time;
      else
        tmp_timeout = 0;
      
      if( tmp_timeout < timeout )
        timeout = tmp_timeout;
    }
  }

  /*
   *
   * RPCROS PROCESSES SELECT() MANAGEMENT
   *
   */

  /* Add to the select() the active RPCROS servers */

  int next_rpcros_server_i = -1;

  for( i = 0; i < CN_MAX_RPCROS_SERVER_CONNECTIONS; i++ )
  {
    int server_fd = tcpIpSocketGetFD( &(n->rpcros_server_proc[i].socket) );

    if( next_rpcros_server_i < 0 &&
        n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_IDLE )
    {
    	next_rpcros_server_i = i;
    }
    else if( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER_SIZE ||
             n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER ||
             n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_SIZE ||
    				 n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING)
    {
      FD_SET( server_fd, &r_fds);
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
    else if( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_WRITING_HEADER ||
             n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_WRITING )
    {
      FD_SET( server_fd, &w_fds);
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
    else if( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_WAIT_FOR_WRITING )
    {
      FD_SET( server_fd, &err_fds);
      if( server_fd > nfds ) nfds = server_fd;
    }
  }

  /* If one RPCROS server is available at least, add to the select() the listner socket */
  if( next_rpcros_server_i >= 0)
  {
    FD_SET( rpcros_listner_fd, &r_fds);
    FD_SET( rpcros_listner_fd, &err_fds);
    if( rpcros_listner_fd > nfds ) nfds = rpcros_listner_fd;
  }

#ifdef DEBUG
  assert(timeout <= n->select_timeout);
#endif

  struct timeval tv = cRosClockGetTimeVal( timeout );

  int n_set = select(nfds + 1, &r_fds, &w_fds, &err_fds, &tv);

  if (n_set == -1)
  {
    if (errno == EINTR)
    {
      PRINT_INFO("cRosNodeDoEventsLoop() : select() returned EINTR\n");
    }
    else
    {
      perror("cRosNodeDoEventsLoop() ");
      exit( EXIT_FAILURE );
    }
  }
  else if( n_set == 0 )
  {
    PRINT_DEBUG ("cRosNodeDoEventsLoop() : select() timeout\n");

    uint64_t cur_time = cRosClockGetTimeMs();

    if(n->xmlrpc_client_proc[0].state == XMLRPC_PROCESS_STATE_IDLE &&
      n->xmlrpc_client_proc[0].wake_up_time_ms <= cur_time )
    {
      n->xmlrpc_client_proc[0].wake_up_time_ms = cur_time + CN_PING_LOOP_PERIOD;

      /* Prepare to ping roscore ... */
      PRINT_DEBUG ( "cRosNodeDoEventsLoop() : Client start writing\n");
      xmlrpcProcessChangeState( &(n->xmlrpc_client_proc[0]), XMLRPC_PROCESS_STATE_WRITING );
    }
    else if( n->xmlrpc_client_proc[0].state != XMLRPC_PROCESS_STATE_IDLE &&
             cur_time - n->xmlrpc_client_proc[0].last_change_time > CN_IO_TIMEOUT )
    {
      /* Timeout between I/O operations... close the socket and re-advertise */
      PRINT_DEBUG ( "cRosNodeDoEventsLoop() : XMLRPC client I/O timeout\n");
      handleXmlrpcClientError( n, 0 );
    }

    for( i = 0; i < CN_MAX_TCPROS_SERVER_CONNECTIONS; i++ )
    {
      int server_fd = tcpIpSocketGetFD( &(n->tcpros_server_proc[i].socket) );
      if( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_WAIT_FOR_WRITING &&
            n->tcpros_server_proc[i].wake_up_time_ms <= cur_time )
      {
        n->tcpros_server_proc[i].wake_up_time_ms = cur_time + n->pubs[n->tcpros_server_proc[i].topic_idx].loop_period;
        tcprosProcessChangeState( &(n->tcpros_server_proc[i]), TCPROS_PROCESS_STATE_START_WRITING );
      }
      else if( (n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER || 
                n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_WRITING ) &&
               cur_time - n->tcpros_server_proc[i].last_change_time > CN_IO_TIMEOUT )
      {
        /* Timeout between I/O operations */
        PRINT_DEBUG ( "cRosNodeDoEventsLoop() : TCPROS server I/O timeout\n");
        handleTcprosServerError( n, i );
      }
    }
  }
  else
  {

    PRINT_DEBUG ( "cRosNodeDoEventsLoop() : select() unblocked\n" );

    for(i = 0; i < CN_MAX_XMLRPC_CLIENT_CONNECTIONS; i++ )
    {
      int xmlrpc_client_fd = tcpIpSocketGetFD( &(n->xmlrpc_client_proc[i].socket) );

      if( FD_ISSET(xmlrpc_client_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : XMLRPC Client error\n" );
        handleXmlrpcClientError( n, i );
      }

      /* Check what is the socket unblocked by the select, and start the requested operations */
      else if( ( n->xmlrpc_client_proc[i].state == XMLRPC_PROCESS_STATE_WRITING && FD_ISSET(xmlrpc_client_fd, &w_fds) ) ||
          ( n->xmlrpc_client_proc[i].state == XMLRPC_PROCESS_STATE_READING && FD_ISSET(xmlrpc_client_fd, &r_fds) ) )
      {
        doWithXmlrpcClientSocket( n, i );
      }
    }
    
    if ( next_xmlrpc_server_i >= 0 )
    {
      if( FD_ISSET( xmlrpc_listner_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : XMLRPC  listner error\n" ); 
      }
      else if( FD_ISSET( xmlrpc_listner_fd, &r_fds) )
      {
        PRINT_DEBUG ( "cRosNodeDoEventsLoop() : XMLRPC listner ready\n" );
        if( tcpIpSocketAccept( &(n->xmlrpc_listner_proc.socket), 
            &(n->xmlrpc_server_proc[next_xmlrpc_server_i].socket) ) == TCPIPSOCKET_DONE &&
            tcpIpSocketSetReuse( &(n->xmlrpc_server_proc[next_xmlrpc_server_i].socket) ) && 
            tcpIpSocketSetNonBlocking( &(n->xmlrpc_server_proc[next_xmlrpc_server_i].socket ) ) )

          xmlrpcProcessChangeState( &(n->xmlrpc_server_proc[next_xmlrpc_server_i]), XMLRPC_PROCESS_STATE_READING );        
      }
    }
    
    for( i = 0; i < CN_MAX_XMLRPC_SERVER_CONNECTIONS; i++ )
    {
      int server_fd = tcpIpSocketGetFD( &(n->xmlrpc_server_proc[i].socket) );
      if( FD_ISSET(server_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : XMLRPC server error\n" );
        tcpIpSocketClose( &(n->xmlrpc_server_proc[i].socket) );
        xmlrpcProcessChangeState( &(n->xmlrpc_server_proc[next_xmlrpc_server_i]), XMLRPC_PROCESS_STATE_IDLE ); 
      }
      else if( ( n->xmlrpc_server_proc[i].state == XMLRPC_PROCESS_STATE_WRITING && FD_ISSET(server_fd, &w_fds) ) || 
               ( n->xmlrpc_server_proc[i].state == XMLRPC_PROCESS_STATE_READING && FD_ISSET(server_fd, &r_fds) ) )
      {
        doWithXmlrpcServerSocket( n, i );
      }
    }

    for(i = 0; i < CN_MAX_TCPROS_CLIENT_CONNECTIONS; i++ )
    {
      TcprosProcess *client_proc = &(n->tcpros_client_proc[i]);
      int tcpros_client_fd = tcpIpSocketGetFD( &(client_proc->socket) );

      if( client_proc->state != TCPROS_PROCESS_STATE_IDLE && FD_ISSET(tcpros_client_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : XMLRPC Client error\n" );
        handleTcprosClientError( n, i );
      }

      if( client_proc->state == TCPROS_PROCESS_STATE_CONNECTING ||
          ( client_proc->state == TCPROS_PROCESS_STATE_WRITING_HEADER && FD_ISSET(tcpros_client_fd, &w_fds) ) ||
          ( client_proc->state == TCPROS_PROCESS_STATE_READING_SIZE && FD_ISSET(tcpros_client_fd, &r_fds) ) ||
          ( client_proc->state == TCPROS_PROCESS_STATE_READING && FD_ISSET(tcpros_client_fd, &r_fds) ) ||
          ( client_proc->state == TCPROS_PROCESS_STATE_READING_HEADER_SIZE && FD_ISSET(tcpros_client_fd, &r_fds) ) ||
          ( client_proc->state == TCPROS_PROCESS_STATE_READING_HEADER && FD_ISSET(tcpros_client_fd, &r_fds) ) )
      {
        doWithTcprosClientSocket( n, i );
      }
    }
    
    if ( next_tcpros_server_i >= 0 )
    {
      if( FD_ISSET( tcpros_listner_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : TCPROS listner error\n" ); 
      }
      else if( FD_ISSET( tcpros_listner_fd, &r_fds) )
      {
        PRINT_DEBUG ( "cRosNodeDoEventsLoop() : TCPROS listner ready\n" );
        if( tcpIpSocketAccept( &(n->tcpros_listner_proc.socket), 
            &(n->tcpros_server_proc[next_tcpros_server_i].socket) ) == TCPIPSOCKET_DONE &&
            tcpIpSocketSetReuse( &(n->tcpros_server_proc[next_tcpros_server_i].socket) ) && 
            tcpIpSocketSetNonBlocking( &(n->tcpros_server_proc[next_tcpros_server_i].socket ) ) &&
            tcpIpSocketSetKeepAlive( &(n->tcpros_server_proc[next_tcpros_server_i].socket ), 60, 10, 9 ) )
        {

          tcprosProcessChangeState( &(n->tcpros_server_proc[next_tcpros_server_i]), TCPROS_PROCESS_STATE_READING_HEADER );
          uint64_t cur_time = cRosClockGetTimeMs();
          n->tcpros_server_proc[next_tcpros_server_i].wake_up_time_ms = cur_time;
        }
      }
    }
    
    for( i = 0; i < CN_MAX_TCPROS_SERVER_CONNECTIONS; i++ )
    {
      int server_fd = tcpIpSocketGetFD( &(n->tcpros_server_proc[i].socket) );
      if( FD_ISSET(server_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : TCPROS server error\n" );
        tcpIpSocketClose( &(n->tcpros_server_proc[i].socket) );
        tcprosProcessChangeState( &(n->tcpros_server_proc[next_tcpros_server_i]), TCPROS_PROCESS_STATE_IDLE ); 
      }
      else if( ( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER && FD_ISSET(server_fd, &r_fds) ) ||
        ( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_START_WRITING && FD_ISSET(server_fd, &w_fds) ) || 
        ( n->tcpros_server_proc[i].state == TCPROS_PROCESS_STATE_WRITING && FD_ISSET(server_fd, &w_fds) ) )
      {
        doWithTcprosServerSocket( n, i );    
      }
    }

    if ( next_rpcros_server_i >= 0 )
    {
      if( FD_ISSET( rpcros_listner_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : TCPROS listner error\n" );
      }
      else if( next_rpcros_server_i >= 0 && FD_ISSET( rpcros_listner_fd, &r_fds) )
      {
        PRINT_DEBUG ( "cRosNodeDoEventsLoop() : TCPROS listner ready\n" );
        if( tcpIpSocketAccept( &(n->rpcros_listner_proc.socket),
            &(n->rpcros_server_proc[next_rpcros_server_i].socket) ) == TCPIPSOCKET_DONE &&
            tcpIpSocketSetReuse( &(n->rpcros_server_proc[next_rpcros_server_i].socket) ) &&
            tcpIpSocketSetNonBlocking( &(n->rpcros_server_proc[next_rpcros_server_i].socket ) ) &&
            tcpIpSocketSetKeepAlive( &(n->rpcros_server_proc[next_rpcros_server_i].socket ), 60, 10, 9 ) )
        {
          tcprosProcessChangeState( &(n->rpcros_server_proc[next_rpcros_server_i]), TCPROS_PROCESS_STATE_READING_HEADER_SIZE );
        }
      }
    }

    for( i = 0; i < CN_MAX_RPCROS_SERVER_CONNECTIONS; i++ )
    {
      int server_fd = tcpIpSocketGetFD( &(n->rpcros_server_proc[i].socket) );
      if( FD_ISSET(server_fd, &err_fds) )
      {
        PRINT_ERROR ( "cRosNodeDoEventsLoop() : TCPROS server error\n" );
        tcpIpSocketClose( &(n->rpcros_server_proc[i].socket) );
        tcprosProcessChangeState( &(n->rpcros_server_proc[next_rpcros_server_i]), TCPROS_PROCESS_STATE_IDLE );
      }
      else if( ( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER_SIZE && FD_ISSET(server_fd, &r_fds) ) ||
        ( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_HEADER && FD_ISSET(server_fd, &r_fds) ) ||
        ( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING_SIZE && FD_ISSET(server_fd, &r_fds) ) ||
        ( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_READING && FD_ISSET(server_fd, &r_fds) ) ||
        ( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_WRITING_HEADER && FD_ISSET(server_fd, &w_fds) ) ||
        ( n->rpcros_server_proc[i].state == TCPROS_PROCESS_STATE_WRITING && FD_ISSET(server_fd, &w_fds) ) )
      {
        doWithRpcrosServerSocket( n, i );
      }
    }

  }
}

void cRosNodeStart( CrosNode *n, unsigned char *exit )
{
  PRINT_VDEBUG ( "cRosNodeStart ()\n" );
  while( !(*exit) )
    cRosNodeDoEventsLoop( n );
}

int enqueueSubscriberAdvertise(CrosNode *node, int subidx)
{
  RosApiCall *call = newRosApiCall();
  if (call == NULL)
  {
    PRINT_ERROR ( "cRosNodeRegisterSubscriber() : Can't allocate memory\n");
    return -1;
  }

  call->provider_idx= subidx;
  call->method = CROS_API_REGISTER_SUBSCRIBER;

  SubscriberNode *sub = &node->subs[subidx];
  xmlrpcParamVectorPushBackString( &call->params, node->name );
  xmlrpcParamVectorPushBackString( &call->params, sub->topic_name );
  xmlrpcParamVectorPushBackString( &call->params, sub->topic_type );
  char node_uri[256];
  snprintf( node_uri, 256, "http://%s:%d/", node->host, node->xmlrpc_port);
  xmlrpcParamVectorPushBackString( &call->params, node_uri );

  XmlrpcProcess *coreproc = &node->xmlrpc_client_proc[0];
  enqueueApiCall(&coreproc->api_calls_queue, call);

  return 0;
}

int enqueuePubliserAdvertise(CrosNode *node, int pubidx)
{
  RosApiCall *call = newRosApiCall();
  if (call == NULL)
  {
    PRINT_ERROR ( "cRosNodeRegisterPublisher() : Can't allocate memory\n");
    return -1;
  }

  call->provider_idx= pubidx;
  call->method = CROS_API_REGISTER_PUBLISHER;

  PublisherNode *publiser = &node->pubs[pubidx];
  xmlrpcParamVectorPushBackString( &call->params, node->name);
  xmlrpcParamVectorPushBackString( &call->params, publiser->topic_name );
  xmlrpcParamVectorPushBackString( &call->params, publiser->topic_type );
  char node_uri[256];
  snprintf( node_uri, 256, "http://%s:%d/", node->host, node->xmlrpc_port);
  xmlrpcParamVectorPushBackString( &call->params, node_uri );

  XmlrpcProcess *coreproc = &node->xmlrpc_client_proc[0];
  enqueueApiCall(&coreproc->api_calls_queue, call);

  return 0;
}

int enqueueServiceAdvertise(CrosNode *node, int serviceidx)
{
  RosApiCall *call = newRosApiCall();
  if (call == NULL)
  {
    PRINT_ERROR ( "cRosNodeRegisterServiceProvider() : Can't allocate memory\n");
    return -1;
  }

  call->provider_idx = serviceidx;
  call->method = CROS_API_REGISTER_SERVICE;

  ServiceProviderNode *service = &node->services[serviceidx];
  xmlrpcParamVectorPushBackString( &call->params, node->name );
  xmlrpcParamVectorPushBackString( &call->params, service->service_name );
  char uri[256];
  snprintf( uri, 256, "rosrpc://%s:%d/", node->host, node->rpcros_port);
  xmlrpcParamVectorPushBackString( &call->params, uri );
  snprintf( uri, 256, "http://%s:%d/", node->host, node->xmlrpc_port);
  xmlrpcParamVectorPushBackString( &call->params, uri );

  XmlrpcProcess *coreproc = &node->xmlrpc_client_proc[0];
  enqueueApiCall(&coreproc->api_calls_queue, call);

  return 0;
}

int enqueueRequestTopic(CrosNode *node, int subidx)
{
  RosApiCall *call = newRosApiCall();
  if (call == NULL)
  {
    PRINT_ERROR ( "cRosNodeRegisterSubscriber() : Can't allocate memory\n");
    return -1;
  }

  call->provider_idx = subidx;
  call->method = CROS_API_REQUEST_TOPIC;

  SubscriberNode *sub = &node->subs[subidx];
  xmlrpcParamVectorPushBackString(&call->params, node->name );
  xmlrpcParamVectorPushBackString(&call->params, sub->topic_name );
  xmlrpcParamVectorPushBackArray(&call->params);
  XmlrpcParam* array_param = xmlrpcParamVectorAt(&call->params,2);
  xmlrpcParamArrayPushBackArray(array_param);
  xmlrpcParamArrayPushBackString(xmlrpcParamArrayGetParamAt(array_param,0),CROS_API_TCPROS_STRING);

  XmlrpcProcess *subproc = &node->xmlrpc_client_proc[sub->client_xmlrpc_id];
  enqueueApiCall(&subproc->api_calls_queue, call);

  return 0;
}

void restartAdversing(CrosNode* n)
{
  int it;
  for(it = 0; it < n->n_pubs; it++)
    enqueuePubliserAdvertise(n, it);

  for(it = 0; it < n->n_subs; it++)
    enqueueSubscriberAdvertise(n, it);

  for(it = 0; it < n->n_services; it++)
    enqueueServiceAdvertise(n, it);
}
