#ifndef REACTOR_HTTP_RESPONSE_H_INCLUDED
#define REACTOR_HTTP_RESPONSE_H_INCLUDED

enum REACTOR_HTTP_RESPONSE_PARSER_STATE
{
  REACTOR_HTTP_RESPONSE_PARSER_HEADER,
  REACTOR_HTTP_RESPONSE_PARSER_CONTENT,
  REACTOR_HTTP_RESPONSE_PARSER_CHUNKED_CONTENT
};

typedef struct reactor_http_response_parser reactor_http_response_parser;
struct reactor_http_response_parser
{
  int                    flags;
  int                    state;
  char                  *base;
  ssize_t                header_size;
  size_t                 chunk_offset;
  reactor_http_response  response;
};


void reactor_http_response_parser_init(reactor_http_response_parser *);
void reactor_http_response_parser_set(reactor_http_response_parser *, int);
void reactor_http_response_parser_data(reactor_http_response_parser *, reactor_stream_data *, reactor_user *);
void reactor_http_response_parser_header(reactor_http_response_parser *, reactor_stream_data *, reactor_user *);
void reactor_http_response_parser_content(reactor_http_response_parser *, reactor_stream_data *, reactor_user *);
void reactor_http_response_parser_chunked_content(reactor_http_response_parser *, reactor_stream_data *, reactor_user *);

#endif /* REACTOR_HTTP_RESPONSE_H_INCLUDED */
