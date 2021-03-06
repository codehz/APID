#include <apid.h>
#include <assert.h>
#include <stdio.h>

static void test1(char const *inp, void *privdata) { printf("recv1: %s\n", inp); }
static void test2(char const *inp, apid_method_reply_ctx *reply) {
  printf("recv2: %s\n", inp);
  apid_method_reply(reply, inp);
}

int main() {
  assert(apid_init() == 0);
  apid_register_action("test1", test1, NULL);
  apid_register_method("test2", test2, NULL);
  apid_hash_set(NULL, NULL, "test-hash", "a", "b");
  apid_hash_set(NULL, NULL, "test-hash", "a", "c");
  apid_hash_set(NULL, NULL, "test-hash", "d", "t");
  apid_set_clear(NULL, NULL, "test3");
  apid_set_add(NULL, NULL, "test3", "a b");
  apid_set_add(NULL, NULL, "test3", "c");
  apid_kv_set(NULL, NULL, "note", "it is the note!");
  apid_start();
}