#include "apid.h"

#include <malloc.h>

#include <adapters/ae.h>
#include <assert.h>
#include <async.h>
#include <hiredis.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#define OPTIONAL_CALLBACK(name, func) (callback ? name : check_error), (callback ? make_bundle((void *)callback, privdata) : (void *)func)

static aeEventLoop *loop          = NULL;
static redisAsyncContext *sub_ctx = NULL;
static redisAsyncContext *ctx     = NULL;

typedef struct callback_bundle {
  void *callback;
  void *privdata;
} callback_bundle;

static callback_bundle *make_bundle(void *callback, void *privdata) {
  callback_bundle *priv = (callback_bundle *)malloc(sizeof(callback_bundle));
  priv->callback        = callback;
  priv->privdata        = privdata;
  return priv;
}

static void free_bundle(callback_bundle *bundle) { free(bundle); }

static void rand_str(char *dest, size_t length) {
  char charset[] =
      "0123456789"
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  while (length-- > 0) {
    size_t index = (double)rand() / RAND_MAX * (sizeof charset - 1);
    *dest++      = charset[index];
  }
  *dest = '\0';
}

static void connectCallback(const redisAsyncContext *c, int status) {
  if (status != REDIS_OK) {
    printf("Error: %s\n", c->errstr);
    aeStop(loop);
    return;
  }
}

static void disconnectCallback(const redisAsyncContext *c, int status) {
  if (status != REDIS_OK) {
    printf("Error: %s\n", c->errstr);
    aeStop(loop);
    return;
  }
  aeStop(loop);
}

static void apid_count_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_count_stub: %s\n", c->errstr);
    exit(1);
  }
  if (!reply || reply->type != REDIS_REPLY_INTEGER) return;
  apid_number_callback callback = (apid_number_callback)priv->callback;
  void *userdata                = priv->privdata;
  free_bundle(priv);
  callback(reply->integer, userdata);
}

static void check_error(redisAsyncContext *c, void *r, void *privdata) {
  if (c->err) {
    printf("%s: %s\n", privdata, c->errstr);
    exit(1);
  }
}

static int post_init() {
  if (sub_ctx->err) {
    printf("Error: %s\n", sub_ctx->errstr);
    return 1;
  }
  if (ctx->err) {
    printf("Error: %s\n", sub_ctx->errstr);
    return 1;
  }
  redisAsyncSetConnectCallback(sub_ctx, connectCallback);
  redisAsyncSetDisconnectCallback(sub_ctx, disconnectCallback);
  redisAsyncSetConnectCallback(ctx, connectCallback);
  redisAsyncSetDisconnectCallback(ctx, disconnectCallback);
  loop = aeCreateEventLoop(64);
  redisAeAttach(loop, sub_ctx);
  redisAeAttach(loop, ctx);
  srand(time(0));
  return 0;
}

int apid_init_unix(char const *path) {
  sub_ctx = redisAsyncConnectUnix(path);
  ctx     = redisAsyncConnectUnix(path);
  return post_init();
}

int apid_init_tcp(char const *ip, int port) {
  sub_ctx = redisAsyncConnect(ip, port);
  ctx     = redisAsyncConnect(ip, port);
  return post_init();
}

int apid_init() {
  char const *addr = getenv("APID") ?: "unix:/tmp/apid.socket";
  if (strncmp(addr, "unix:", 5) == 0) {
    return apid_init_unix(addr + 5);
  } else if (strncmp(addr, "tcp:", 4) == 0) {
    char const *part = addr + 4;
    char const *port = strchr(part, ':');
    if (!port) return -1;
    char buffer[0x100] = { 0 };
    strncpy(buffer, part, port - part);
    return apid_init_tcp(buffer, atoi(port + 1));
  }
  return -2;
}

int apid_start() {
  if (loop)
    aeMain(loop);
  else
    return -1;
  return 0;
}

int apid_stop() {
  if (loop)
    aeStop(loop);
  else
    return -1;
  return 0;
}

char const *apid_underlying_impl() { return "redis"; }
void *apid_underlying_context() { return (void *)ctx; }

static void action_callback_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv         = (callback_bundle *)privdata;
  apid_action_callback callback = (apid_action_callback)priv->callback;
  void *userdata                = priv->privdata;
  redisReply *reply             = (redisReply *)r;
  if (c->err) {
    printf("apid_register_action: %s\n", c->errstr);
    exit(1);
  }
  if (!reply || reply->type != REDIS_REPLY_ARRAY || strcmp(reply->element[0]->str, "message") != 0) return;
  callback(reply->element[2]->str, userdata);
}

int apid_register_action(char const *name, apid_action_callback callback, void *privdata) {
  return redisAsyncCommand(sub_ctx, action_callback_stub, make_bundle((void *)callback, privdata), "SUBSCRIBE %s", name);
}

struct apid_method_reply_ctx {
  void *privdata;
  char buffer[0];
};

static void method_callback_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv         = (callback_bundle *)privdata;
  apid_method_callback callback = (apid_method_callback)priv->callback;
  void *userdata                = priv->privdata;
  redisReply *reply             = (redisReply *)r;
  if (c->err) {
    printf("apid_register_method: %s\n", c->errstr);
    exit(1);
  }
  if (!reply || reply->type != REDIS_REPLY_ARRAY || strcmp(reply->element[0]->str, "psubscribe") == 0) return;
  char *full                       = reply->element[2]->str;
  int len                          = strlen(full);
  apid_method_reply_ctx *reply_ctx = (apid_method_reply_ctx *)malloc(sizeof(void *) + len + 1);
  reply_ctx->privdata              = userdata;
  strncpy((char *)&reply_ctx->buffer, full, len);
  callback(reply->element[3]->str, reply_ctx);
}

int apid_register_method(char const *name, apid_method_callback callback, void *privdata) {
  return redisAsyncCommand(sub_ctx, method_callback_stub, make_bundle((void *)callback, privdata), "PSUBSCRIBE %s@*", name);
}

int apid_method_reply(apid_method_reply_ctx *reply, char const *content) {
  int ret = redisAsyncCommand(ctx, NULL, NULL, "LPUSH %s %s", reply->buffer, content);
  free(reply);
  return ret;
}

static void apid_zero_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv       = (callback_bundle *)privdata;
  apid_zero_callback callback = (apid_zero_callback)priv->callback;
  void *userdata              = priv->privdata;
  redisReply *reply           = (redisReply *)r;
  if (c->err) {
    printf("apid_zero_stub: %s\n", c->errstr);
    exit(1);
  }
  free_bundle(priv);
  callback(userdata);
}

int apid_invoke(apid_zero_callback callback, void *privdata, char const *name, char const *argument) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_zero_stub, "apid_invoke"), "PUBLISH %s %s", name, argument);
}

static void apid_invoke_method_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_invoke_method_stub: %s\n", c->errstr);
    exit(1);
  }
  if (!reply) return;
  if (reply->type == REDIS_REPLY_STRING) {
    printf("apid_invoke_method_stub: %s\n", reply->str);
    exit(1);
  }
  if (reply->type != REDIS_REPLY_ARRAY) return;
  apid_data_callback callback = (apid_data_callback)priv->callback;
  void *userdata              = priv->privdata;
  free_bundle(priv);
  callback(reply->element[1]->str, userdata);
}

int apid_invoke_method(apid_data_callback callback, void *privdata, char const *name, char const *argument) {
  if (callback) {
    char unq[0x10];
    rand_str(unq, 0x10);
    int ret = redisAsyncCommand(ctx, check_error, (void *)"apid_invoke_method", "PUBLISH %s@%s %s", name, unq, argument);
    ret |= redisAsyncCommand(ctx, apid_invoke_method_stub, make_bundle((void *)callback, privdata), "BRPOP %s@%s 0", name, unq);
    return ret;
  }
  int ret = redisAsyncCommand(ctx, check_error, (void *)"apid_invoke_method", "PUBLISH %s@ignore %s", name, argument);
  ret |= redisAsyncCommand(ctx, check_error, (void *)"apid_invoke_method", "BRPOP %s@ignore 0", name);
  return ret;
}

int apid_kv_set(apid_zero_callback callback, void *privdata, char const *name, char const *value) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_zero_stub, "apid_kv_set"), "SET %s %s", name, value);
}

static void apid_data_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_data_stub: %s\n", c->errstr);
    exit(1);
  }
  if (!reply) return;
  if (reply->type != REDIS_REPLY_STRING) return;
  apid_data_callback callback = (apid_data_callback)priv->callback;
  void *userdata              = priv->privdata;
  free_bundle(priv);
  callback(reply->str, userdata);
}

int apid_kv_get(apid_data_callback callback, void *privdata, char const *name) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_data_stub, "apid_kv_get"), "GET %s", name);
}

int apid_publish(char const *name, char const *data) {
  return redisAsyncCommand(ctx, check_error, (void *)"apid_publish", "PUBLISH %s %s", name, data);
}

static void apid_subscibe_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_subscribe: %s\n", c->errstr);
    exit(1);
  }
  if (!reply || reply->type != REDIS_REPLY_ARRAY || strcmp(reply->element[0]->str, "message") != 0) return;
  apid_data_callback callback = (apid_data_callback)priv->callback;
  void *userdata              = priv->privdata;
  callback(reply->str, userdata);
}

int apid_subscribe(apid_data_callback callback, void *privdata, char const *name) {
  assert(callback);
  return redisAsyncCommand(sub_ctx, apid_subscibe_stub, make_bundle((void *)callback, privdata), "SUBSCRIBE %s", name);
}

static void apid_subscibe_pattern_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_register_method: %s\n", c->errstr);
    exit(1);
  }
  if (!reply || reply->type != REDIS_REPLY_ARRAY || strcmp(reply->element[0]->str, "psubscribe") == 0) return;
  apid_data2_callback callback = (apid_data2_callback)priv->callback;
  void *userdata               = priv->privdata;
  callback(reply->element[2]->str, reply->element[3]->str, userdata);
}

int apid_subscribe_pattern(apid_data2_callback callback, void *privdata, char const *pattern) {
  assert(callback);
  return redisAsyncCommand(sub_ctx, apid_subscibe_pattern_stub, make_bundle((void *)callback, privdata), "PSUBSCRIBE %s", pattern);
}

int apid_set_clear(apid_number_callback callback, void *privdata, char const *key) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_count_stub, "apid_set_clear"), "DEL %s", key);
}

int apid_set_add(apid_number_callback callback, void *privdata, char const *key, char const *value) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_count_stub, "apid_set_add"), "SADD %s %s", key, value);
}

int apid_set_remove(apid_number_callback callback, void *privdata, char const *key, char const *value) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_count_stub, "apid_set_remove"), "SREM %s %s", key, value);
}

static void apid_set_iterate_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_set_iterate: %s\n", c->errstr);
    exit(1);
  }
  if (!reply || reply->type != REDIS_REPLY_ARRAY) return;
  apid_data_done_callback callback = (apid_data_done_callback)priv->callback;
  void *userdata                   = priv->privdata;
  free_bundle(priv);
  for (int i = 0; i < reply->elements; i++) callback(false, reply->element[i]->str, userdata);
  callback(true, NULL, userdata);
}

int apid_set_iterate(apid_data_done_callback callback, void *privdata, char const *key) {
  assert(callback);
  return redisAsyncCommand(ctx, apid_set_iterate_stub, make_bundle((void *)callback, privdata), "SMEMBERS %s", key);
}

static void apid_set_contains_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_set_contains: %s\n", c->errstr);
    exit(1);
  }
  if (!reply || reply->type != REDIS_REPLY_INTEGER) return;
  apid_bool_callback callback = (apid_bool_callback)priv->callback;
  void *userdata              = priv->privdata;
  free_bundle(priv);
  callback(!!reply->integer, userdata);
}

int apid_set_contains(apid_bool_callback callback, void *privdata, char const *key, char const *value) {
  assert(callback);
  return redisAsyncCommand(ctx, apid_set_contains_stub, make_bundle((void *)callback, privdata), "SISMEMBER %s %s", key, value);
}

int apid_hash_clear(apid_number_callback callback, void *privdata, char const *key) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_count_stub, "apid_hash_clear"), "DEL %s", key);
}

int apid_hash_set(apid_number_callback callback, void *privdata, char const *key, char const *hkey, char const *hvalue) {
  return redisAsyncCommand(ctx, OPTIONAL_CALLBACK(apid_count_stub, "apid_hash_set"), "HSET %s %s %s", key, hkey, hvalue);
}

static void apid_hash_get_stub(redisAsyncContext *c, void *r, void *privdata) {
  callback_bundle *priv = (callback_bundle *)privdata;
  redisReply *reply     = (redisReply *)r;
  if (c->err) {
    printf("apid_hash_get: %s\n", c->errstr);
    exit(1);
  }
  if (!reply) return;
  if (reply->type == REDIS_REPLY_NIL) {
    apid_data_flag_callback callback = (apid_data_flag_callback)priv->callback;
    void *userdata                   = priv->privdata;
    free_bundle(priv);
    callback(false, NULL, userdata);
  } else if (reply->type == REDIS_REPLY_STRING) {
    apid_data_flag_callback callback = (apid_data_flag_callback)priv->callback;
    void *userdata                   = priv->privdata;
    free_bundle(priv);
    callback(true, reply->str, userdata);
  }
}

int apid_hash_get(apid_data_flag_callback callback, void *privdata, char const *key, char const *hkey) {
  assert(callback);
  return redisAsyncCommand(ctx, apid_hash_get_stub, make_bundle((void *)callback, privdata), "HGET %s %s", key, hkey);
}