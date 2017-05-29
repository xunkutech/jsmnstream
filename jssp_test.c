#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_passed = 0;
static int test_failed = 0;

static char null_reg[1] =
    { '\0' };

#include "jssp.c"

typedef struct
{
  jsspliteral_t type;
  char *js;
  size_t len;
  size_t offset;
  const char *start;
  size_t size;
  char reg[7];
  jssperr_t ret;
} literalparser_t;

#define check_result(err,o,r,lpt) do { \
    if (err != lpt.ret) \
    { \
      printf("Test %s failed: Returned code does not match.\n", o); \
      test_failed ++; \
      return 1; \
    } \
    if (strlen(o) != lpt.size) \
    { \
      printf("Test %s failed: Returned size does not match.\n", o); \
      test_failed ++; \
      return 1; \
    } \
    if (memcmp(lpt.start, o, lpt.size) != 0) \
    { \
      printf("Test %s failed: Returned content does not match.\n", o); \
      test_failed ++; \
      return 1; \
    } \
    if (memcmp(lpt.reg,r, r[0]) != 0) \
    { \
      printf("Test %s failed: Returned register does not match.\n", o); \
      test_failed ++; \
      return 1; \
    } \
    printf("Test passed.\n"); \
    test_passed ++; \
} while(0)

#define test_string_parser(i,o,err,r) do { \
  literalparser_t lpt;  \
  lpt.reg[0] = 0; \
  lpt.type = JSSP_STRING; \
  lpt.js= i; \
  lpt.len = strlen (lpt.js); \
  lpt.offset = 0; \
  lpt.start = NULL; \
  lpt.size = 0; \
  lpt.ret = jssp_parse_literal(lpt.type, lpt.js, lpt.len, &lpt.offset, &lpt.start, &lpt.size, lpt.reg); \
  check_result(err,o,r,lpt); \
} while(0)

#define test_string_broken(i,b,e,o,r,e2,o2,r2,e3,o3,r3) do { \
  literalparser_t lpt;  \
  lpt.reg[0] = 0; \
  lpt.type = JSSP_STRING; \
  lpt.js= i; \
  lpt.len = b; \
  lpt.offset = 0; \
  lpt.start = NULL; \
  lpt.size = 0; \
  lpt.ret = jssp_parse_literal(lpt.type, lpt.js, lpt.len, &lpt.offset, &lpt.start, &lpt.size, lpt.reg); \
  check_result(e,o,r,lpt); \
  lpt.start = NULL; \
  lpt.len = strlen(lpt.js); \
  lpt.ret = jssp_parse_literal(lpt.type, lpt.js, lpt.len, &lpt.offset, &lpt.start, &lpt.size, lpt.reg); \
  check_result(e2,o2,r2,lpt); \
  lpt.start = NULL; \
  lpt.ret = jssp_parse_literal(lpt.type, lpt.js, lpt.len, &lpt.offset, &lpt.start, &lpt.size, lpt.reg); \
  check_result(e3,o3,r3,lpt); \
} while(0)

int
test_literal_parser ()
{
  char r[7];

  test_string_parser("I am string.\"xxx",
                     "I am string.",
                     JSSP_SUCCESS,
                     null_reg);

  test_string_parser("I am string.",
                     "I am string.",
                     JSSP_ERROR_BROKEN,
                     null_reg);

  test_string_parser("str\\uabcd\"",
                     "str\\uabcd",
                     JSSP_SUCCESS,
                     null_reg);

  test_string_parser("str\\uabc\"",
                     "",
                     JSSP_ERROR_INVAL,
                     null_reg);

  test_string_parser("str\\\"xxx\"",
                     "str\\\"xxx",
                     JSSP_SUCCESS,
                     null_reg);

  test_string_parser("str\\txxx\"",
                     "str\\txxx",
                     JSSP_SUCCESS,
                     null_reg);

  test_string_parser("str\\xxx\"",
                     "",
                     JSSP_ERROR_INVAL,
                     null_reg);

  test_string_parser("str\\ufffg",
                     "",
                     JSSP_ERROR_INVAL,
                     null_reg);

  r[0] = 5;
  memcpy (r + 1, "\\uabc", 5);
  test_string_parser("str\\uabc",
                     "str",
                     JSSP_ERROR_BROKEN,
                     r);
  r[0] = 2;
  memcpy (r + 1, "\\u", 2);
  test_string_parser("str\\u",
                     "str",
                     JSSP_ERROR_BROKEN,
                     r);

  r[0] = 1;
  memcpy (r + 1, "\\", 1);
  test_string_parser("str\\",
                     "str",
                     JSSP_ERROR_BROKEN,
                     r);

  r[0] = 5;
  memcpy (r + 1, "\\uabc", 5);
  test_string_broken("str\\uabcdxxx\"",
                     8,
                     JSSP_ERROR_BROKEN,
                     "str",
                     r,
                     JSSP_ERROR_BROKEN,
                     "\\uabcd",
                     null_reg,
                     JSSP_SUCCESS,
                     "xxx",
                     null_reg
                     );

  r[0] = 1;
  r[1] = '\\';
  test_string_broken("str\\txxx\"",
                     4,
                     JSSP_ERROR_BROKEN,
                     "str",
                     r,
                     JSSP_ERROR_BROKEN,
                     "\\t",
                     null_reg,
                     JSSP_SUCCESS,
                     "xxx",
                     null_reg
                     );

  r[0] = 1;
  r[1] = "中文"[0];
  test_string_broken("utf8中文xxx\"",
                     5,
                     JSSP_ERROR_BROKEN,
                     "utf8",
                     r,
                     JSSP_ERROR_BROKEN,
                     "中",
                     null_reg,
                     JSSP_SUCCESS,
                     "文xxx",
                     null_reg
                     );

  r[0] = 2;
  r[1] = "中"[0];
  r[2] = "中"[1];
  test_string_broken("utf8中文xxx\"",
                     6,
                     JSSP_ERROR_BROKEN,
                     "utf8",
                     r,
                     JSSP_ERROR_BROKEN,
                     "中",
                     null_reg,
                     JSSP_SUCCESS,
                     "文xxx",
                     null_reg
                     );

  test_string_broken("utf8中文xxx\"",
                     7,
                     JSSP_ERROR_BROKEN,
                     "utf8中",
                     null_reg,
                     JSSP_SUCCESS,
                     "文xxx",
                     null_reg,
                     JSSP_ERROR_BROKEN,
                     "",
                     null_reg
                     );

  r[0] = 1;
  r[1] = "文"[0];
  test_string_broken("utf8中文\"",
                     8,
                     JSSP_ERROR_BROKEN,
                     "utf8中",
                     null_reg,
                     JSSP_SUCCESS,
                     "文",
                     null_reg,
                     JSSP_ERROR_BROKEN,
                     "",
                     null_reg
                     );

  r[0] = 2;
  r[1] = "文"[0];
  r[2] = "文"[1];
  test_string_broken("utf8中文\"",
                     9,
                     JSSP_ERROR_BROKEN,
                     "utf8中",
                     null_reg,
                     JSSP_SUCCESS,
                     "文",
                     null_reg,
                     JSSP_ERROR_BROKEN,
                     "",
                     null_reg
                     );
  return 0;
}
typedef struct
{
  jssptype_t type;
  size_t depth;
  size_t index;
  char *key;
  char *data;
  int enable;
} testjsoncb_t;

typedef struct _testjson_t testjson_t;
struct _testjson_t
{
  testjsoncb_t *cbt;
  int cbt_count;
  int cbt_index;
  jssp_parser p;
  char *js;
  size_t js_len;
  void *buf;
  size_t buf_size;
  size_t max_key_len;
  jssperr_t err;

};

#define check(cbt, t, dp, idx, k, kl, d, ds) do { \
  if(cbt.enable != 0) \
      break; \
  if(cbt.type != t) \
    { \
      printf("Test failed: Returned json node type does not match. R: %d, C:%d\n", t, cbt.type); \
      test_failed ++; \
      return 1; \
    } \
  if(cbt.depth != dp) \
    { \
      printf("Test failed: Returned json node depth does not match.R: %zu, C:%zu\n", dp, cbt.depth); \
      test_failed ++; \
      return 1; \
    } \
  if(cbt.index != idx) \
    { \
      printf("Test failed: Returned json node index does not match.\n"); \
      test_failed ++; \
      return 1; \
    } \
  if(NULL != cbt.key && (strlen(cbt.key) != kl || strncmp(cbt.key, k, kl) != 0)) \
    { \
      printf("Test failed: Returned json key does not match. %s %zu %.*s\n",cbt.key, kl, (int)kl, k); \
      test_failed ++; \
      return 1; \
    } \
  if(NULL != cbt.data && (strlen(cbt.data) != ds || strncmp(cbt.data, d, ds) != 0)) \
    { \
      printf("Test failed: Returned json data does not match.\n"); \
      test_failed ++; \
      return 1; \
    } \
  printf("Test passed.\n"); \
  test_passed ++; \
}while(0)

int
test_jssp_parser_cb (void *cls,
                     jssptype_t type,
                     size_t depth,
                     size_t index,
                     const char *key,
                     size_t key_len,
                     const char *data,
                     size_t data_size,
                     uint64_t stream_offset)
{
  testjson_t *jt = (testjson_t *) cls;
  testjsoncb_t cbt = jt->cbt[jt->cbt_index++];
  check(cbt, type, depth, index, key, key_len, data, data_size);
  return 0;
}

#define test_json_string(s,t) do { \
  testjson_t jt; \
  jt.buf = malloc(1000); \
  jt.js = s; \
  jt.cbt = t; \
  jt.cbt_index = 0; \
  jssp_init(&jt.p); \
  jt.err = jssp_parse(&jt.p,jt.js,strlen(jt.js),jt.buf,10000,100,&test_jssp_parser_cb,&jt); \
}while(0)

int
test_basic_json_parser ()
{
  testjsoncb_t cbt[10];
  int i;

  i = -1;
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_OPEN;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "a";
  cbt[i].data = "b";
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_CLOSE;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  test_json_string("{\"a\":\r\t\n \"b\"}", cbt);

  i = -1;
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_OPEN;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "a";
  cbt[i].data = "123";
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_CLOSE;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  test_json_string("{\"a\":123}", cbt);

  i = -1;
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_OPEN;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "a";
  cbt[i].data = "true";
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_CLOSE;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  test_json_string("{\"a\":true}", cbt);

  i = -1;
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_OPEN;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_ARRAY_OPEN;
  cbt[i].key = "a";
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 0;
  cbt[i].type = JSSP_ARRAY_VAL;
  cbt[i].key = NULL;
  cbt[i].data = "1";
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 1;
  cbt[i].type = JSSP_ARRAY_VAL;
  cbt[i].key = NULL;
  cbt[i].data = "2";
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_ARRAY_CLOSE;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_CLOSE;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  test_json_string("{\"a\":[1,2]}", cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 1;
  cbt[i].type = JSSP_ARRAY_OPEN;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 4;
  cbt[i].index = 0;
  cbt[i].type = JSSP_ARRAY_VAL;
  cbt[i].key = NULL;
  cbt[i].data = "2";
  cbt[++i].enable = 0;
  cbt[i].depth = 4;
  cbt[i].index = 1;
  cbt[i].type = JSSP_ARRAY_VAL;
  cbt[i].key = NULL;
  cbt[i].data = "3";
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  test_json_string("{\"a\":[1,[2,3]]}", cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_OPEN;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 4;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "a";
  cbt[i].data = "1";
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_CLOSE;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 1;
  cbt[i].type = JSSP_ARRAY_VAL;
  cbt[i].key = NULL;
  cbt[i].data = "true";
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  test_json_string("{\"a\":[{\"a\":1},true]}", cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 1;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "c";
  cbt[i].data = "2";
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  test_json_string("{\"a\":{\"b\":1,\"c\":2}}", cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 3;
  cbt[i].index = 1;
  cbt[i].type = JSSP_ARRAY_OPEN;
  cbt[i].key = "c";
  cbt[i].data = NULL;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 4;
  cbt[i].index = 1;
  cbt[i].type = JSSP_ARRAY_VAL;
  cbt[i].key = NULL;
  cbt[i].data = "3";
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  test_json_string("{\"a\":{\"b\":1,\"c\":[2,3]}}", cbt);

  return 0;
}

#define test_json_string_broken(s,b,t) do { \
    testjson_t jt; \
    jt.buf = malloc(1000); \
    jt.js = s; \
    jt.cbt = t; \
    jt.cbt_index = 0; \
    jssp_init(&jt.p); \
    jt.err = jssp_parse(&jt.p,jt.js,b,jt.buf,10000,100,&test_jssp_parser_cb,&jt); \
    jt.err = jssp_parse(&jt.p,jt.js,strlen(jt.js),jt.buf,10000,100,&test_jssp_parser_cb,&jt); \
} while(0)

int
test_part_json_parser ()
{
  testjsoncb_t cbt[10];
  int i;

  i = -1;
  cbt[++i].enable = 0;
  cbt[i].depth = 1;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_OPEN;
  cbt[i].key = NULL;
  cbt[i].data = NULL;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "str\\uabcd";
  cbt[i].data = "str\\ufedc";
  cbt[++i].enable = 1;
  test_json_string_broken("{\"str\\uabcd\":\"str\\ufedc\"}", 8, cbt);;

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "str\\uabcd";
  cbt[i].data = "str";
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "str\\uabcd";
  cbt[i].data = "\\ufedc";
  cbt[++i].enable = 1;
  test_json_string_broken("{\"str\\uabcd\":\"str\\ufedc\"}", 20, cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "utf8中";
  cbt[i].data = "utf8文";
  cbt[++i].enable = 1;
  test_json_string_broken("{\"utf8中\":\"utf8文\"}", 8, cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "utf8中";
  cbt[i].data = "utf8";
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "utf8中";
  cbt[i].data = "文";
  cbt[++i].enable = 1;
  test_json_string_broken("{\"utf8中\":\"utf8文\"}", 18, cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "utf8中xxx";
  cbt[i].data = "utf8文yyy";
  cbt[++i].enable = 1;
  test_json_string_broken("{\"utf8中xxx\":\"utf8文yyy\"}", 8, cbt);

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "utf8中xxx";
  cbt[i].data = "utf8";
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "utf8中xxx";
  cbt[i].data = "文";
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_OBJECT_VAL;
  cbt[i].key = "utf8中xxx";
  cbt[i].data = "yyy";
  cbt[++i].enable = 1;
  test_json_string_broken("{\"utf8中xxx\":\"utf8文yyy\"}", 21, cbt);

  return 0;
}

#define test_json_nomem(s,t,max_node,max_key_len) do { \
  size_t bufsize= sizeof(jsspnode_t)*(max_node + 1) + max_key_len; \
  testjson_t jt; \
  jt.buf = calloc(1,bufsize); \
  jt.js = s; \
  jt.cbt = t; \
  jt.cbt_index = 0; \
  jssp_init(&jt.p); \
  jt.err = jssp_parse(&jt.p,jt.js,strlen(jt.js),jt.buf,bufsize,max_key_len,&test_jssp_parser_cb,&jt); \
  if(jt.err != JSSP_ERROR_NOMEM) \
    { \
      printf("Test failed.\n"); \
    } \
  bufsize *= 2; \
  jt.buf = realloc(jt.buf, bufsize); \
  jt.err = jssp_parse(&jt.p,jt.js,strlen(jt.js),jt.buf,bufsize,max_key_len,&test_jssp_parser_cb,&jt); \
}while(0)

int
test_nomem ()
{
  testjsoncb_t cbt[30];
  int i;

  i = -1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 2;
  cbt[i].index = 0;
  cbt[i].type = JSSP_ARRAY_OPEN;
  cbt[i].key = "a";
  cbt[i].data = NULL;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 0;
  cbt[i].depth = 12;
  cbt[i].index = 0;
  cbt[i].type = JSSP_ARRAY_VAL;
  cbt[i].key = NULL;
  cbt[i].data = "1";
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  cbt[++i].enable = 1;
  test_json_nomem("{\"a\":[[[[[[[[[[1]]]]]]]]]]}", cbt, 6, 2);
  return 0;
}

void
main ()
{
  test_literal_parser ();
  test_basic_json_parser ();
  test_part_json_parser ();
  test_nomem ();
}
