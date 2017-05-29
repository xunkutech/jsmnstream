#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../jssp.h"

#ifndef REST_DEBUG
#define rest_debug(M, ...)
#else
#include <stdio.h>
#define rest_debug(M, ...) do { fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
#endif

#define REST_ERROR_NOMEM -1
#define REST_SUCCESS 0
#define REST_ERROR_INVAL 1

#define REST_NAME "resname"
#define REST_PROPS "properties"
#define REST_DATASET "dataset"
#define REST_DATA "data"
#define REST_FILTER "filter"
#define REST_SORT "sort"
#define REST_LIMIT "limit"

#define REST_CONDITION_SIZE 1
#define REST_OUTPUT_SIZE 2048

/*
 * AND, OR, BETWEEN, IN, NOTIN, STARTWITH, ENDWITH, LIKE, GT, LT, GE, LE, EQ, NE, ISNULL, NOTNULL
 * */

/* the only valid json string is one-pair key-value object*/
#define QUERY_CONDITION1 "[\"BETWEEN\",\"field\",\"value1\",\"value2\"]"
#define QUERY_CONDITION2 "[\"AND\",[\"GE\",\"field1\",\"value1\"],[\"LIKE\",\"field2\",\"%value2\"]]"
#define QUERY_CONDITION3 "[\"AND\",[\"GE\",\"field1\",\"value1\"],[\"OR\",[\"EQ\",\"field2\",\"value2\"],[\"IN\",\"field3\",\"value3\",\"value4\",\"value5\"]]]"
#define QUERY_CONDITION4 "{\"resname\":\"t1\",\"properties\":[\"col1\",\"col2\"],\"filter\":[\"BETWEEN\",\"field\",\"value1\",\"value2\"]}"
#define QUERY_CONDITION5 "{\"resname\":\"t1\",\"properties\":[\"col1\",\"col2\"],\"dataset\":[[\"v1\",\"v2\"],[\"v3\",\"v4\"]],\"data\":[\"v1\",\"v2\"],\"filter\":[\"AND\",[\"GE\",\"field1\",\"value1\"],[\"OR\",[\"EQ\",\"field2\",\"value2\"],[\"IN\",\"field3\",\"value3\",\"value4\",\"value5\"]]]}"

/*
 POST/CREATE {"resname":"t1","properties":["col1","col2"],"dataset":[["v1","v2"],["v3","v4"]]}
 GET/READ {"resname":"t1","properties":["col1", "col2"],"filter":[]}
 POST/UPDATE {"resname":"t1","properties":["col1","col2"],"data":["v1","v2"],"filter":[]}
 DELETE {"resname":"t1","filter":[]}
 */

typedef enum
{
  AND,
  OR,
  BETWEEN,
  NOTBETWEEN,
  IN,
  NOTIN,
  LIKE,
  NOTLIKE,
  MATCH,
  NOTMATCH,
  GT,
  LT,
  GE,
  LE,
  EQ,
  NE,
  ISNULL,
  NOTNULL,
  UNKNOWN
} rest_filter_op;

typedef enum
{
  REST_BIND_INTEGER,
  REST_BIND_REAL,
  REST_BIND_TEXT,
  REST_BIND_NUMERIC,
  REST_BIND_NULL
} rest_bind_type;

typedef struct
{
  size_t var_start;
  size_t var_count;
  size_t operator;
  char* column_name;
  rest_bind_type column_type;
  size_t parent;
} rest_filter_t;

typedef enum
{
  RESTKEY_RESNAME,
  RESTKEY_PROPERTIES,
  RESTKEY_DATASET,
  RESTKEY_DATA,
  RESTKEY_FILTER,
  RESTKEY_INIT
} rest_json_key;

typedef struct _rest_jsonparser_cls rest_jsonparser_cls;

struct _rest_jsonparser_cls
{
  char *resname;

  char *filter;

  /* tmp string arrays */
  char **filter_varholder;
  size_t filter_varholder_size;
  size_t filter_varholder_pos;

  rest_filter_t **filter_stack;
  size_t filter_stack_size;
  size_t filter_stack_pos;

  int status;

  size_t index_guard;

  int
  (*rest_init_cb) (rest_jsonparser_cls *rjp);
  int
  (*rest_finish_cb) (rest_jsonparser_cls *rjp);
  int
  (*rest_insert_cb) (rest_jsonparser_cls *rjp);

  void **db_cls;
};

#define rest_append_string(str, data, data_size) do { \
  if (NULL != (str)) \
    { \
      rest_debug("reallocate string with addition size %zu", (size_t)data_size); \
      char *nstr = realloc ((str), strlen (str) + (size_t)data_size + 1); \
      if (NULL == nstr ) \
        { \
          rest_debug("Can not allocat memory for %s with addtion size %zu", str, (size_t)data_size); \
          return REST_ERROR_NOMEM; \
        } \
        (str) = nstr; \
    } \
  else \
    { \
      rest_debug("malloc string with new size %zu", (size_t)data_size + 1); \
      char *nstr = malloc ((size_t)data_size + 1); \
      if (NULL == (nstr)) \
        { \
          rest_debug("Can not allocat memory size %zu",  (size_t)data_size + 1); \
          return REST_ERROR_NOMEM; \
        } \
      nstr[0] = '\0'; \
      (str) = nstr; \
    } \
  strncat((str), data, data_size); \
}while (0)

#define rest_compare_opstr(key, opstr) \
  else if (strcmp ((key), #opstr) == 0) { ret = opstr; }

#define rest_convert_opstr2type(key) ({ \
    rest_filter_op ret = UNKNOWN; \
    if(NULL == key){} \
    rest_compare_opstr(key, AND) \
    rest_compare_opstr(key, OR) \
    rest_compare_opstr(key, BETWEEN) \
    rest_compare_opstr(key, NOTBETWEEN) \
    rest_compare_opstr(key, IN) \
    rest_compare_opstr(key, NOTIN) \
    rest_compare_opstr(key, LIKE) \
    rest_compare_opstr(key, NOTLIKE) \
    rest_compare_opstr(key, GT) \
    rest_compare_opstr(key, LT) \
    rest_compare_opstr(key, GE) \
    rest_compare_opstr(key, LE) \
    rest_compare_opstr(key, EQ) \
    rest_compare_opstr(key, NE) \
    rest_compare_opstr(key, ISNULL) \
    rest_compare_opstr(key, NOTNULL) \
    ret; \
})

#define rest_release_cls(rjp) do { \
  if (NULL != (rjp)) \
    { \
      while((rjp)->filter_varholder_pos > 0) \
        { \
          free((rjp)->filter_varholder[(rjp)->filter_varholder_pos - 1]); \
          (rjp)->filter_varholder_pos--; \
        } \
      while((rjp)->filter_stack_pos > 0) \
        { \
          free((rjp)->filter_stack[(rjp)->filter_stack_pos - 1]); \
          (rjp)->filter_stack_pos--; \
        } \
    } \
} while (0)

#define rest_init_cls(rjpcls) do { \
  (rjpcls)->resname = NULL; \
  (rjpcls)->filter = NULL; \
  (rjpcls)->rest_init_cb = &rest_init; \
  (rjpcls)->rest_finish_cb = &rest_finish; \
  (rjpcls)->rest_insert_cb = &rest_insert; \
  (rjpcls)->filter_varholder = NULL; \
  (rjpcls)->filter_varholder_size = 0; \
  (rjpcls)->filter_varholder_pos = 0; \
  (rjpcls)->filter_stack = NULL; \
  (rjpcls)->filter_stack_size = 0; \
  (rjpcls)->filter_stack_pos = 0; \
} while (0)

int
rest_init (rest_jsonparser_cls *rjp)
{
  rest_debug("Begining rest");
  return REST_SUCCESS;
}

int
rest_finish (rest_jsonparser_cls *rjp)
{
  rest_debug("Ending rest");
  return REST_SUCCESS;
}

int
rest_insert (rest_jsonparser_cls *rjp)
{
  rest_debug("Insert data");
  return REST_SUCCESS;
}

#define REST_FILTERSTACK_SIZE 50
#define rest_realloc_filter(rjp) do { \
  rest_debug("realloc_filter"); \
  (rjp)->filter_stack_pos++; \
  if ((rjp)->filter_stack_pos > (rjp)->filter_stack_size) \
    { \
      (rjp)->filter_stack_size = (rjp)->filter_stack_size < 1 ? \
                                 REST_FILTERSTACK_SIZE : \
                                 (rjp)->filter_stack_size * 2 + 1; \
      rest_filter_t **new_filterstack = realloc ((rjp)->filter_stack, \
                                                sizeof(rest_filter_t *) \
                                                * (rjp)->filter_stack_size); \
      if (NULL == new_filterstack) \
        return REST_ERROR_NOMEM; \
      (rjp)->filter_stack = new_filterstack; \
    } \
    (rjp)->filter_stack[(rjp)->filter_stack_pos - 1] = calloc(1, sizeof(rest_filter_t)); \
    if (NULL == (rjp)->filter_stack[(rjp)->filter_stack_pos - 1]) \
      return REST_ERROR_NOMEM; \
} while (0)

#define REST_VARHOLDER_SIZE 50
#define rest_realloc_varholder(rjp) do { \
  rest_debug("realloc_varholder"); \
  (rjp)->filter_varholder_pos++; \
  if ((rjp)->filter_varholder_pos > (rjp)->filter_varholder_size) \
    { \
      (rjp)->filter_varholder_size = (rjp)->filter_varholder_size < 1 ? \
                               REST_VARHOLDER_SIZE : \
                               (rjp)->filter_varholder_size * 2; \
      char **new_varholder = realloc ((rjp)->filter_varholder, \
                                              sizeof(char *) \
                                              * (rjp)->filter_varholder_size); \
      if (NULL == new_varholder) \
        return REST_ERROR_NOMEM; \
      rjp->filter_varholder = new_varholder; \
    } \
} while (0)

#define rest_store_value(rjp,index,data,data_size) do { \
  __typeof__ (index) _i = index;\
  if ((rjp)->filter_stack_pos == 0) \
    return REST_ERROR_INVAL; \
  rest_filter_t filter = (rjp)->filter_stack[(rjp)->filter_stack_pos - 1]; \
  if ((rjp)->filter_varholder_size < filter->operator + _i) \
    { \
      rest_realloc_varholder(rjp); \
    } \
  rest_append_string((rjp)->filter_varholder[filter->operator + _i], data, data_size); \
  (rjp)->filter_varholder_pos = filter->operator + _i + 1; \
  filter->var_count = index + 1; \
} while (0)

#define rest_resolve_column(filter) do { \
    filter->column_name = "col"; \
} while (0)

int
rest_parse_filter (rest_jsonparser_cls *rjp,
                   jssptype_t type,
                   size_t index,
                   const char *data,
                   size_t data_size)
{
  rest_filter_t *filter;
  rest_debug("starting filter parser %d", type);
  switch (type)
    {
    case JSSP_ARRAY_OPEN:
      if (rjp->filter_stack_pos > 0 && index > 1)
        {
          rest_debug("conject logic operator AND OR");
          filter = rjp->filter_stack[rjp->filter_stack_pos - 1];
          switch (rest_convert_opstr2type(rjp->filter_varholder[filter->parent]))
            {
            case AND:
              rest_append_string(rjp->filter, " AND (", 6);
              break;
            case OR:
              rest_append_string(rjp->filter, " OR (", 5);
              break;
            default:
              rest_debug("Invalid OP format. %s",rjp->filter_varholder[filter->operator] );
              return REST_ERROR_INVAL;
            }
        }
      else
        {
          rest_append_string(rjp->filter, "(", 1);
        }
      rest_realloc_filter(rjp);
      filter = rjp->filter_stack[rjp->filter_stack_pos - 1];
      filter->operator = rjp->filter_varholder_pos;
      filter->column_name = NULL;
      if (index > 0)
        {
          filter->parent = rjp->filter_stack[rjp->filter_stack_pos - 2]->parent;
        }
      else
        {
          filter->parent = rjp->filter_stack_pos - 2;
        }
      rest_debug("Create new filter object at %zu", rjp->filter_stack_pos -1);
      break;
    case JSSP_ARRAY_CLOSE:
      filter = rjp->filter_stack[rjp->filter_stack_pos - 1];
      rest_filter_op opt = rest_convert_opstr2type(rjp->filter_varholder[filter
        ->operator]);
      int s;
      char *complete_result;

      /* resolve column name and column type */
      rest_resolve_column(filter);
      switch (opt)
        {
        case BETWEEN:

        case NOTBETWEEN:

          if (filter->var_count < 4)
            return REST_ERROR_INVAL;

          s = asprintf (&complete_result,
                        "%s %s ? AND ?",
                        filter->column_name,
                        ((BETWEEN == opt) ? "BETWEEN" : "NOT BETWEEN"));
          if (s < 0)
            return REST_ERROR_NOMEM;

          rest_append_string(rjp->filter, complete_result, s);
          free (complete_result);

          rest_debug("Temp output %s", rjp->filter);
          break;
        case IN:

        case NOTIN:
          if (filter->var_count < 3)
            return REST_ERROR_INVAL;

          s = asprintf (&complete_result,
                        "%s %s (?",
                        filter->column_name,
                        ((IN == opt) ? "IN" : "NOT IN"));

          if (s < 0)
            return REST_ERROR_NOMEM;

          rest_append_string(rjp->filter, complete_result, s);
          free (complete_result);

          complete_result = malloc ((filter->var_count - 3) * 2 + 1);
          if (NULL == complete_result)
            return REST_ERROR_NOMEM;

          int i;
          for (i = 0; i < filter->var_count - 3; i++)
            {
              complete_result[2 * i] = ',';
              complete_result[2 * i + 1] = '?';
            }
          complete_result[2 * i] = ')';

          rest_append_string(rjp->filter,
                             complete_result,
                             (filter->var_count - 3) * 2 + 1);
          free (complete_result);
          rest_debug("Temp output %s", rjp->filter);
          break;
        case ISNULL:

        case NOTNULL:
          if (filter->var_count < 2)
            return REST_ERROR_INVAL;

          s = asprintf (&complete_result,
                        "%s %s",
                        filter->column_name,
                        ((ISNULL == opt) ? "ISNULL" : "NOTNULL"));

          if (s < 0)
            return REST_ERROR_NOMEM;

          rest_append_string(rjp->filter, complete_result, s);
          free (complete_result);
          rest_debug("Temp output %s", rjp->filter);
          break;
        case LIKE:

        case NOTLIKE:

        case MATCH:

        case NOTMATCH:

        case GT:

        case LT:

        case GE:

        case LE:

        case EQ:

        case NE:
          if (filter->var_count != 3)
            return REST_ERROR_INVAL;

          char *p;
          switch (opt)
            {
            case GT:
              p = ">";
              break;
            case LT:
              p = "<";
              break;
            case GE:
              p = ">=";
              break;
            case LE:
              p = "<=";
              break;
            case EQ:
              p = "==";
              break;
            case NE:
              p = "!=";
              break;
            case LIKE:
              p = "LIKE";
              break;
            case NOTLIKE:
              p = "NOT LIKE";
              break;
            case MATCH:
              p = "MATCH";
              break;
            case NOTMATCH:
              p = "NOT MATCH";
              break;
            default:
              return REST_ERROR_INVAL;
            }

          s = asprintf (&complete_result,
                        "%s %s ?",
                        filter->column_name,
                        p);

          if (s < 0)
            return REST_ERROR_NOMEM;

          rest_append_string(rjp->filter, complete_result, s);
          free (complete_result);

          rest_debug("Temp output %s", rjp->filter);
          break;
        case AND:

        case OR:
          break;
        default:
          rest_debug("Bad OP TYPE  %d\n", __LINE__);
          return REST_ERROR_INVAL;
        }

      rest_append_string(rjp->filter, ")", 1);
      rest_debug("Temp output %s", rjp->filter);
      break;
    case JSSP_ARRAY_VAL:
      rest_debug ("parsing value: %.*s at index %zu", (int) data_size, data, index);
      if ((rjp)->filter_stack_pos == 0)
        return REST_ERROR_INVAL;

      filter = rjp->filter_stack[rjp->filter_stack_pos - 1];
      rest_debug ("varholder size: %zu, operator index %zu", rjp->filter_varholder_size, filter->operator);

      if (rjp->filter_varholder_size < filter->operator + index + 1)
        {
          rest_realloc_varholder(rjp);
        }

      rest_append_string(rjp->filter_varholder[filter->operator + index],
                         data,
                         data_size);
      rest_debug ("parsing value: %.*s", (int) data_size, data);
      rjp->filter_varholder_pos = filter->operator + index + 1;
      filter->var_count = index + 1;
      break;
    default:
      rest_debug("Invalid json object");
      return REST_ERROR_INVAL;
    }
  rest_debug("Parse filter for type %d success.", type);
  return REST_SUCCESS;
}

int
rest_parse_properteis (rest_jsonparser_cls *rjp,
                       jssptype_t type,
                       size_t index,
                       const char *data,
                       size_t data_size)
{

  return REST_SUCCESS;
}

int
rest_parse_data (rest_jsonparser_cls *rjp,
                 jssptype_t type,
                 size_t index,
                 const char *data,
                 size_t data_size)
{

  return REST_SUCCESS;
}

int
rest_parse_dataset (rest_jsonparser_cls *rjpcls,
                    jssptype_t type,
                    size_t index,
                    const char *data,
                    size_t data_size)
{
  int ret = REST_SUCCESS;

  if (JSSP_ARRAY_VAL == type)
    {
      ret = rest_parse_data (rjpcls, type, index, data, data_size);
    }

  if (JSSP_ARRAY_CLOSE == type)
    {

    }
  return ret;
}

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
  int ret;
  rest_jsonparser_cls *rjpcls = (rest_jsonparser_cls *) cls;
  if (NULL == rjpcls)
    {
      ret = REST_ERROR_INVAL;
      goto ERROR;
    }

  if (depth == 1 && JSSP_OBJECT_OPEN == type)
    {
      return rjpcls->rest_init_cb (rjpcls);
    }

  if (depth == 1 && JSSP_OBJECT_CLOSE == type)
    {
      return rjpcls->rest_finish_cb (rjpcls);
    }

  if (depth == 2 && NULL != key)
    {
      rest_debug("Key is %.*s, type is %d",(int)key_len, key, type);
      if (strlen (REST_NAME) == key_len
        && strncmp (REST_NAME, key, key_len) == 0)
        {
          rest_append_string(rjpcls->resname, data, data_size);
          rjpcls->status = RESTKEY_RESNAME;
        }
      else if (strlen (REST_PROPS) == key_len
        && strncmp (REST_PROPS, key, key_len) == 0)
        rjpcls->status = RESTKEY_PROPERTIES;
      else if (strlen (REST_DATASET) == key_len
        && strncmp (REST_DATASET, key, key_len) == 0)
        rjpcls->status = RESTKEY_DATASET;
      else if (strlen (REST_DATA) == key_len
        && strncmp (REST_DATA, key, key_len) == 0)
        rjpcls->status = RESTKEY_DATA;
      else if (strlen (REST_FILTER) == key_len
        && strncmp (REST_FILTER, key, key_len) == 0)
        {
          rjpcls->status = RESTKEY_FILTER;
        }
    }

  if (depth >= 2)
    {
      switch (rjpcls->status)
        {
        case RESTKEY_FILTER:
          rest_debug("Calling parse filter");
          return rest_parse_filter (rjpcls, type, index, data, data_size);
        case RESTKEY_PROPERTIES:
          rest_debug("Calling parse properties");
          return rest_parse_properteis (rjpcls, type, index, data, data_size);
        case RESTKEY_DATA:
          rest_debug("Calling parse data");
          return rest_parse_data (rjpcls, type, index, data, data_size);
        case RESTKEY_DATASET:
          rest_debug("Calling parse dataset");
          return rest_parse_dataset (rjpcls, type, index, data, data_size);
        default:
          break;
        }
    }
  return 0;
  ERROR:
  return ret;
}

int
main ()
{

  rest_jsonparser_cls cls;
  rest_init_cls(&cls);

  jssp_parser p;
  jssp_init (&p);

  void *buf = malloc (1000);
  jssp_parse (&p,
  QUERY_CONDITION5,
              strlen (QUERY_CONDITION5),
              buf,
              1000,
              100,
              &test_jssp_parser_cb,
              &cls);

  rest_release_cls(&cls);

  return 0;
}

