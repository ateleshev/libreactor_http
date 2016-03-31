#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <regex.h>
#include <netdb.h>

#include <dynamic.h>
#include <clo.h>
#include <reactor_core.h>
#include <reactor_net.h>

#include "reactor_http.h"

static const char *reactor_http_response_message[] =
  {
    [100] = "Continue",
    [101] = "Switching Protocols",
    [200] = "OK",
    [201] = "Created",
    [202] = "Accepted",
    [203] = "Non-Authoritative Information",
    [204] = "No Content",
    [205] = "Reset Content",
    [206] = "Partial Content",
    [300] = "Multiple Choices",
    [301] = "Moved Permanently",
    [302] = "Found",
    [303] = "See Other",
    [305] = "Use Proxy",
    [306] = "(Unused)",
    [307] = "Temporary Redirect",
    [400] = "Bad Request",
    [401] = "Unauthorized",
    [402] = "Payment Required",
    [403] = "Forbidden",
    [404] = "Not Found",
    [405] = "Method Not Allowed",
    [406] = "Not Acceptable",
    [407] = "Proxy Authentication Required",
    [408] = "Request Timeout",
    [409] = "Conflict",
    [410] = "Gone",
    [411] = "Length Required",
    [412] = "Precondition Failed",
    [413] = "Request Entity Too Large",
    [414] = "Request-URI Too Long",
    [415] = "Unsupported Media Type",
    [416] = "Requested Range Not Satisfiable",
    [417] = "Expectation Failed",
    [500] = "Internal Server Error",
    [501] = "Not Implemented",
    [502] = "Bad Gateway",
    [503] = "Service Unavailable",
    [504] = "Gateway Timeout",
    [505] = "HTTP Version Not Supported"
  };

char *reactor_http_split_match(regmatch_t *, char *, char *);

int reactor_http_split_url(char *url, char **host, char **service, char **path)
{
  regmatch_t match[6];
  regex_t reg;
  int e;

  e = regcomp(&reg, "^http://([^/?#:]*)(:([^/]+))?(/(.*))?", REG_EXTENDED);
  if (e == -1)
    return -1;

  e = regexec(&reg, url, 6, match, 0);
  regfree(&reg);
  if (e == REG_NOMATCH)
    return -1;

  *host = reactor_http_split_match(&match[1], url, "localhost");
  *service = reactor_http_split_match(&match[3], url, "80");
  *path = reactor_http_split_match(&match[5], url, "");
  return 0;
}

char *reactor_http_split_match(regmatch_t *match, char *string, char *def)
{
  if (match->rm_so == -1 || match->rm_eo == -1)
    return def;

  string[match->rm_eo] = '\0';
  return &string[match->rm_so];
}

int reactor_http_field_add_range(vector *fields, char *key, size_t key_len, char *value, size_t value_len)
{
  reactor_http_field field;

  field = (reactor_http_field) {.key = key, .value = value};
  key[key_len] = 0;
  value[value_len] = 0;
  return vector_push_back(fields, &field);
}

char *reactor_http_field_lookup(vector *fields, char *key)
{
  size_t i;
  reactor_http_field *field;

  for (i = 0; i < vector_size(fields); i ++)
    {
      field = vector_at(fields, i);
      if (strcasecmp(field->key, key) == 0)
        return field->value;
    }
  return NULL;
}

void reactor_http_field_offset(vector *fields, off_t offset)
{
  size_t i;
  reactor_http_field *field;

  for (i = 0; i < vector_size(fields); i ++)
    {
      field = vector_at(fields, i);
      field->key += offset;
      field->value += offset;
    }
}

void reactor_http_request_init(reactor_http_request *request)
{
  *request = (reactor_http_request) {0};
  vector_init(&request->fields, sizeof(reactor_http_field));
}

void reactor_http_request_clear(reactor_http_request *request)
{
  vector_clear(&request->fields);
}

void reactor_http_request_create(reactor_http_request *request, char *host, char *service, char *method, char *path, char *content, size_t content_size)
{
  *request = (reactor_http_request)
    {
      .method = method,
      .host = host,
      .service = service,
      .path = path,
      .content = content,
      .content_size = content_size
    };
    vector_init(&request->fields, sizeof(reactor_http_field));
}

void reactor_http_request_add_header(reactor_http_request *request, char *key, char *value)
{
  vector_push_back(&request->fields, (reactor_http_field[]){{.key = key, .value = value}});
}

void reactor_http_request_send(reactor_http_request *request, reactor_stream *stream)
{
  size_t i;
  reactor_http_field *field;

  reactor_stream_puts(stream, request->method);
  reactor_stream_puts(stream, " /");
  reactor_stream_puts(stream, request->path);
  reactor_stream_puts(stream, " HTTP/1.1\r\n");
  reactor_stream_puts(stream, "Host: ");
  reactor_stream_puts(stream, request->host);
  reactor_stream_puts(stream, "\r\n");
  for (i = 0; i < vector_size(&request->fields); i ++)
    {
      field = (reactor_http_field *) vector_at(&request->fields, i);
      if (field->key && field->value)
        {
          reactor_stream_puts(stream, field->key);
          reactor_stream_puts(stream, ": ");
          reactor_stream_puts(stream, field->value);
          reactor_stream_puts(stream, "\r\n");
        }
    }
  reactor_stream_puts(stream, "\r\n");
}

void reactor_http_response_init(reactor_http_response *response)
{
  *response = (reactor_http_response) {0};
  vector_init(&response->fields, sizeof(reactor_http_field));
}

void reactor_http_response_create(reactor_http_response *response, unsigned status, char *content, size_t content_size)
{
  reactor_http_response_init(response);
  response->status = status;
  response->minor_version = 1;
  response->content = content;
  response->content_size = content_size;
  if (status < sizeof reactor_http_response_message / sizeof reactor_http_response_message[0] &&
      reactor_http_response_message[status])
    response->message = (char *) reactor_http_response_message[status];
  else
    response->message = "Undefined";
}

void reactor_http_response_add_header(reactor_http_response *response, char *key, char *value)
{
  vector_push_back(&response->fields, (reactor_http_field[]){{.key = key, .value = value}});
}

void reactor_http_response_send(reactor_http_response *response, reactor_stream *stream)
{
  size_t i;
  reactor_http_field *field;

  reactor_stream_puts(stream, "HTTP/1.1 ");
  reactor_stream_putu(stream, response->status);
  reactor_stream_puts(stream, " ");
  reactor_stream_puts(stream, response->message);
  reactor_stream_puts(stream, "\r\n");
  reactor_stream_puts(stream, "Content-Length: ");
  reactor_stream_putu(stream, response->content_size);
  reactor_stream_puts(stream, "\r\n");
  for (i = 0; i < vector_size(&response->fields); i ++)
    {
      field = (reactor_http_field *) vector_at(&response->fields, i);
      if (field->key && field->value)
        {
          reactor_stream_puts(stream, field->key);
          reactor_stream_puts(stream, ": ");
          reactor_stream_puts(stream, field->value);
          reactor_stream_puts(stream, "\r\n");
        }
    }
  reactor_stream_puts(stream, "\r\n");
  if (response->content_size)
    reactor_stream_write(stream, response->content, response->content_size);
}

void reactor_http_response_clear(reactor_http_response *response)
{
  vector_clear(&response->fields);
}
