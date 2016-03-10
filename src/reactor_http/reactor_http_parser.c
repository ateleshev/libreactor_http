#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netdb.h>

#include <dynamic.h>
#include <reactor_core.h>

#include "picohttpparser.h"
#include "reactor_http.h"
#include "reactor_http_parser.h"

void reactor_http_parser_init(reactor_http_parser *parser, reactor_user_call *call, void *state)
{
  *parser = (reactor_http_parser) {.state = REACTOR_HTTP_PARSER_CLOSED};
  reactor_user_init(&parser->user, call, state);
}

void reactor_http_parser_open_response(reactor_http_parser *parser, reactor_http_response *response, int flags)
{
  parser->state = REACTOR_HTTP_PARSER_RESPONSE_HEADER;
  parser->flags = flags | REACTOR_HTTP_PARSER_FLAGS_RESPONSE;
  parser->response = response;
}

void reactor_http_parser_open_request(reactor_http_parser *parser, reactor_http_request *request, int flags)
{
  parser->state = REACTOR_HTTP_PARSER_REQUEST_HEADER;
  parser->flags = flags;
  parser->request = request;
}

void reactor_http_parser_error(reactor_http_parser *parser)
{
  if (parser->state == REACTOR_HTTP_PARSER_CLOSED)
    return;

  reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_ERROR, NULL);
  reactor_http_parser_close(parser);
}

void reactor_http_parser_close(reactor_http_parser *parser)
{
  parser->state = REACTOR_HTTP_PARSER_CLOSED;
}

void reactor_http_parser_data(reactor_http_parser *parser, reactor_stream_data *data)
{
  switch(parser->state)
    {
    case REACTOR_HTTP_PARSER_REQUEST_HEADER:
      reactor_http_parser_request_header(parser, data);
      break;
    case REACTOR_HTTP_PARSER_RESPONSE_HEADER:
      reactor_http_parser_response_header(parser, data);
      break;
    case REACTOR_HTTP_PARSER_BODY:
      reactor_http_parser_body(parser, data);
      break;
    case REACTOR_HTTP_PARSER_CHUNKED_BODY:
      reactor_http_parser_chunked_body(parser, data);
      break;
    case REACTOR_HTTP_PARSER_FINAL:
      reactor_http_parser_final(parser, data);
      break;
    default:
      break;
    }
}

void reactor_http_parser_request_header(reactor_http_parser *parser, reactor_stream_data *data)
{
  reactor_http_request *request;
  size_t fields_count, method_size, path_size,  i;
  struct phr_header fields[REACTOR_HTTP_PARSER_MAX_FIELDS];
  int n;
  char *value;

  request = parser->request;
  parser->base = data->base;
  fields_count = REACTOR_HTTP_PARSER_MAX_FIELDS;
  n = phr_parse_request(data->base, data->size,
                        (const char **) &request->method, &method_size,
                        (const char **) &request->path, &path_size,
                        &request->minor_version,
                        fields, &fields_count, 0);
  if (n < 0)
    {
      if (n == -1)
        reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_ERROR, NULL);
      return;
    }

  request->method[method_size] = '\0';
  request->path[path_size] = '\0';
  for (i = 0; i < fields_count; i ++)
    reactor_http_field_add_range(&request->fields, (char *) fields[i].name, fields[i].name_len,
                                 (char *) fields[i].value, fields[i].value_len);

  parser->content_begin = n;
  parser->content_end = n;
  value = reactor_http_field_lookup(&request->fields, "transfer-encoding");
  if (value && strcasecmp(value, "chunked") == 0)
    {
      request->content_size = 0;
      parser->chunk_begin = n;
      parser->state = REACTOR_HTTP_PARSER_CHUNKED_BODY;
    }
  else
    {
      value = reactor_http_field_lookup(&request->fields, "content-length");
      request->content_size = value ? strtoul(value, NULL, 0) : 0;
      parser->size = n + request->content_size;
      parser->state = REACTOR_HTTP_PARSER_BODY;
    }

  reactor_http_parser_data(parser, data);
}

void reactor_http_parser_response_header(reactor_http_parser *parser, reactor_stream_data *data)
{
  reactor_http_response *response;
  size_t message_size, fields_count, i;
  struct phr_header fields[REACTOR_HTTP_PARSER_MAX_FIELDS];
  char *value;
  int n;

  response = parser->response;
  parser->base = data->base;
  fields_count = REACTOR_HTTP_PARSER_MAX_FIELDS;
  n = phr_parse_response(data->base, data->size,
                         &response->minor_version,
                         &response->status,
                         (const char **) &response->message, &message_size,
                         fields, &fields_count, 0);
  if (n < 0)
    {
      if (n == -1)
        reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_ERROR, NULL);
      return;
    }

  for (i = 0; i < fields_count; i ++)
    reactor_http_field_add_range(&response->fields, (char *) fields[i].name, fields[i].name_len,
                                 (char *) fields[i].value, fields[i].value_len);

  if (parser->flags & REACTOR_HTTP_PARSER_FLAGS_STREAM)
    {
      reactor_http_parser_response_final(parser, data);
      reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_HEADER, &parser->response);
      reactor_stream_data_consume(data, n);
      n = 0;
    }

  parser->content_begin = n;
  parser->content_end = n;
  value = reactor_http_field_lookup(&response->fields, "transfer-encoding");
  if (value && strcasecmp(value, "chunked") == 0)
    {
      response->content_size = 0;
      parser->chunk_begin = n;
      parser->state = REACTOR_HTTP_PARSER_CHUNKED_BODY;
    }
  else
    {
      value = reactor_http_field_lookup(&response->fields, "content-length");
      response->content_size = value ? strtoul(value, NULL, 0) : 0;
      parser->size = n + response->content_size;
      parser->state = REACTOR_HTTP_PARSER_BODY;
    }

  reactor_http_parser_data(parser, data);
}

void reactor_http_parser_body(reactor_http_parser *parser, reactor_stream_data *data)
{
  size_t size;

  if (data->size && parser->flags & REACTOR_HTTP_PARSER_FLAGS_STREAM)
    {
      size = MIN(parser->size, data->size);
      reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_CHUNK, (reactor_stream_data[]){{.base = data->base, .size = size}});
      reactor_stream_data_consume(data, size);
      parser->size -= size;
    }

  if (data->size < parser->size)
    return;

  parser->content_end = parser->size;
  parser->state = REACTOR_HTTP_PARSER_FINAL;
  reactor_http_parser_data(parser, data);
}

void reactor_http_parser_chunked_body(reactor_http_parser *parser, reactor_stream_data *data)
{
  char *start, *end, *eol;
  size_t size;

  start = data->base + parser->chunk_begin;
  end = data->base + data->size;
  eol = memmem(start, end - start, "\r\n", 2);
  if (!eol)
    return;
  size = strtoul(start, NULL, 16);
  start = eol + 2;
  if (size > (size_t) (end - start))
    return;

  if (parser->flags & REACTOR_HTTP_PARSER_FLAGS_STREAM)
    {
      reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_CHUNK, (reactor_stream_data[]){{.base = start, .size = size}});
      reactor_stream_data_consume(data, (start - data->base) + size + 2);
    }
  else
    {
      memmove(data->base + parser->content_end, start, size);
      parser->content_end += size;
      parser->chunk_begin = (start - data->base) + size + 2;
      parser->size = parser->content_end;
    }

  if (size == 0)
    parser->state = REACTOR_HTTP_PARSER_FINAL;

  reactor_http_parser_data(parser, data);
}

void reactor_http_parser_final(reactor_http_parser *parser, reactor_stream_data *data)
{
  if (parser->flags & REACTOR_HTTP_PARSER_FLAGS_RESPONSE)
    {
      reactor_http_parser_response_final(parser, data);
      reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_DONE, &parser->response);
      parser->state = REACTOR_HTTP_PARSER_RESPONSE_HEADER;
    }
  else
    {
      reactor_http_parser_request_final(parser, data);
      reactor_user_dispatch(&parser->user, REACTOR_HTTP_PARSER_DONE, &parser->request);
      parser->state = REACTOR_HTTP_PARSER_REQUEST_HEADER;
    }

  reactor_stream_data_consume(data, parser->size);
  //reactor_http_parser_close(parser);
}

void reactor_http_parser_request_final(reactor_http_parser *parser, reactor_stream_data *data)
{
  reactor_http_request *request;
  off_t offset;

  request = parser->request;
  request->content = data->base + parser->content_begin;
  request->content_size = parser->content_end - parser->content_begin;
  offset = data->base - parser->base;
  if (offset)
    {
      request->method += offset;
      request->path += offset;
      reactor_http_field_offset(&request->fields, offset);
    }
}

void reactor_http_parser_response_final(reactor_http_parser *parser, reactor_stream_data *data)
{
  reactor_http_response *response;
  off_t offset;

  response = parser->response;
  response->content = data->base + parser->content_begin;
  response->content_size = parser->content_end - parser->content_begin;
  offset = data->base - parser->base;
  if (offset)
    {
      response->message += offset;
      reactor_http_field_offset(&response->fields, offset);
    }
}
