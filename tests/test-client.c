#include <apid.h>
#include <assert.h>
#include <stdio.h>

void method_callback(char const *reply, void *priv) { printf("Got reply: %s\n", reply); }

void prop_callback(char const *reply, void *priv) { printf("Got note: %s\n", reply); }

void set_detect(bool contain, void *priv) { printf("detect(%s): %d\n", priv, contain); }

void set_iter(bool stop, char const *data, void *priv) {
  if (stop) {
    apid_stop();
    return;
  }
  printf("SET -> %s\n", data);
}

void hash_test(bool nonempty, char const *data, void *priv) { printf("hash: %d %s\n", nonempty, data); }

int main() {
  assert(apid_init() == 0);
  apid_kv_get(prop_callback, NULL, "note");
  apid_invoke(NULL, NULL, "test1", "test from client");
  apid_invoke_method(method_callback, NULL, "test2", "test2 from client");
  apid_set_contains(set_detect, "a c", "test3", "a c");
  apid_set_contains(set_detect, "c", "test3", "c");
  apid_set_remove(NULL, NULL, "test3", "a b");
  apid_hash_get(hash_test, NULL, "test-hash", "a");
  apid_set_iterate(set_iter, NULL, "test3");
  apid_start();
}