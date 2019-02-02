#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct apid_method_reply_ctx apid_method_reply_ctx;
typedef void (*apid_zero_callback)(void *privdata);
typedef void (*apid_data_callback)(char const *data, void *privdata);
typedef void (*apid_bool_callback)(bool value, void *privdata);
typedef void (*apid_data_flag_callback)(bool flag, char const *data, void *privdata);
typedef void (*apid_data_done_callback)(bool done, char const *data, void *privdata);
typedef void (*apid_data2_callback)(char const *identify, char const *data, void *privdata);
typedef void (*apid_action_callback)(char const *, void *privdata);
typedef void (*apid_method_callback)(char const *, apid_method_reply_ctx *);

int apid_init();
int apid_init_unix(char const *path);
int apid_init_tcp(char const *ip, int port);
int apid_start();
int apid_stop();

char const *apid_underlying_impl();
void *apid_underlying_context();

int apid_register_action(char const *name, apid_action_callback callback, void *privdata) __attribute__((nonnull(2)));
int apid_register_method(char const *name, apid_method_callback callback, void *privdata) __attribute__((nonnull(2)));
int apid_method_reply(apid_method_reply_ctx *, char const *content) __attribute__((nonnull(1, 2)));

int apid_invoke(apid_zero_callback callback, void *privdata, char const *name, char const *argument);
int apid_invoke_method(apid_data_callback callback, void *privdata, char const *name, char const *argument);

int apid_kv_set(apid_zero_callback callback, void *privdata, char const *name, char const *value);
int apid_kv_get(apid_data_callback callback, void *privdata, char const *name) __attribute__((nonnull(1)));

int apid_publish(char const *name, char const *data);
int apid_subscribe(apid_data_callback callback, void *privdata, char const *name) __attribute__((nonnull(1)));
int apid_subscribe_pattern(apid_data2_callback callback, void *privdata, char const *pattern) __attribute__((nonnull(1)));

int apid_set_clear(apid_zero_callback callback, void *privdata, char const *key);
int apid_set_add(apid_zero_callback callback, void *privdata, char const *key, char const *value);
int apid_set_remove(apid_zero_callback callback, void *privdata, char const *key, char const *value);
int apid_set_iterate(apid_data_done_callback callback, void *privdata, char const *key) __attribute__((nonnull(1)));
int apid_set_contains(apid_bool_callback callback, void *privdata, char const *key, char const *value) __attribute__((nonnull(1)));

int apid_hash_clear(apid_zero_callback callback, void *privdata, char const *key);
int apid_hash_set(apid_zero_callback callback, void *privdata, char const *key, char const *hkey, char const *hvalue);
int apid_hash_get(apid_data_flag_callback callback, void *privdata, char const *key, char const *hkey) __attribute__((nonnull(1)));

#ifdef __cplusplus
}
#endif