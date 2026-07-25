/* json_test.c — unit tests for the json module. */
#include "json/json.h"

#include "hbi_test.h"

#include <string.h>

static hbi_json_value *parse_ok(const char *s) {
    hbi_json_value *root = NULL;
    hbi_status st = hbi_json_parse(s, strlen(s), NULL, &root);
    HBI_CHECK_EQ_INT(st, HBI_OK);
    return root;
}

static void expect_fail(const char *s) {
    hbi_json_value *root = NULL;
    hbi_status st = hbi_json_parse(s, strlen(s), NULL, &root);
    HBI_CHECK(st != HBI_OK);
    HBI_CHECK(root == NULL);
}

static void test_scalars(void) {
    hbi_json_value *v = parse_ok("42");
    HBI_CHECK(hbi_json_is_number(v));
    HBI_CHECK(hbi_json_as_number(v) == 42.0);
    int64_t i = 0;
    HBI_CHECK_EQ_INT(hbi_json_as_int(v, &i), HBI_OK);
    HBI_CHECK_EQ_INT(i, 42);
    hbi_json_free(v, NULL);

    v = parse_ok("true");
    HBI_CHECK(hbi_json_is_bool(v) && hbi_json_as_bool(v));
    hbi_json_free(v, NULL);

    v = parse_ok("null");
    HBI_CHECK(hbi_json_kind_of(v) == HBI_JSON_NULL);
    hbi_json_free(v, NULL);

    v = parse_ok("\"hello\"");
    HBI_CHECK(hbi_json_is_string(v));
    HBI_CHECK_STR_EQ(hbi_json_as_string(v), "hello");
    hbi_json_free(v, NULL);
}

static void test_number_forms(void) {
    hbi_json_value *v = parse_ok("-1.5e-3");
    HBI_CHECK(hbi_json_is_number(v));
    HBI_CHECK(hbi_json_as_number(v) < 0.0);
    hbi_json_free(v, NULL);

    /* Non-integral rejected by as_int. */
    v = parse_ok("3.14");
    int64_t i = 0;
    HBI_CHECK(hbi_json_as_int(v, &i) != HBI_OK);
    hbi_json_free(v, NULL);

    /* Large integer (like a data offset). */
    v = parse_ok("4294967296");
    HBI_CHECK_EQ_INT(hbi_json_as_int(v, &i), HBI_OK);
    HBI_CHECK_EQ_INT(i, 4294967296LL);
    hbi_json_free(v, NULL);
}

static void test_object(void) {
    const char *doc = "{\"hidden_size\": 2880, \"arch\": \"gpt_oss\", \"flag\": false}";
    hbi_json_value *v = parse_ok(doc);
    HBI_CHECK(hbi_json_is_object(v));
    HBI_CHECK_EQ_INT(hbi_json_object_count(v), 3);

    const hbi_json_value *hs = hbi_json_object_get(v, "hidden_size");
    HBI_CHECK(hbi_json_is_number(hs));
    HBI_CHECK(hbi_json_as_number(hs) == 2880.0);

    const hbi_json_value *arch = hbi_json_object_get(v, "arch");
    HBI_CHECK_STR_EQ(hbi_json_as_string(arch), "gpt_oss");

    HBI_CHECK(hbi_json_object_get(v, "missing") == NULL);
    hbi_json_free(v, NULL);
}

static void test_array_nested(void) {
    /* Mimics a safetensors shape + data_offsets. */
    const char *doc = "{\"shape\": [2880, 5760], \"data_offsets\": [0, 33177600]}";
    hbi_json_value *v = parse_ok(doc);
    const hbi_json_value *shape = hbi_json_object_get(v, "shape");
    HBI_CHECK(hbi_json_is_array(shape));
    HBI_CHECK_EQ_INT(hbi_json_array_count(shape), 2);
    HBI_CHECK(hbi_json_as_number(hbi_json_array_at(shape, 0)) == 2880.0);
    HBI_CHECK(hbi_json_as_number(hbi_json_array_at(shape, 1)) == 5760.0);

    const hbi_json_value *off = hbi_json_object_get(v, "data_offsets");
    int64_t end = 0;
    HBI_CHECK_EQ_INT(hbi_json_as_int(hbi_json_array_at(off, 1), &end), HBI_OK);
    HBI_CHECK_EQ_INT(end, 33177600);
    hbi_json_free(v, NULL);
}

static void test_string_escapes(void) {
    hbi_json_value *v = parse_ok("\"a\\nb\\t\\u0041\"");
    HBI_CHECK_STR_EQ(hbi_json_as_string(v), "a\nb\tA");
    hbi_json_free(v, NULL);
}

static void test_empty_containers(void) {
    hbi_json_value *v = parse_ok("{}");
    HBI_CHECK(hbi_json_is_object(v) && hbi_json_object_count(v) == 0);
    hbi_json_free(v, NULL);
    v = parse_ok("[]");
    HBI_CHECK(hbi_json_is_array(v) && hbi_json_array_count(v) == 0);
    hbi_json_free(v, NULL);
}

static void test_malformed(void) {
    expect_fail("{");
    expect_fail("{\"a\":}");
    expect_fail("[1,2,");
    expect_fail("\"unterminated");
    expect_fail("truX");
    expect_fail("{\"a\":1} extra");
    expect_fail("");
}

static void test_selftest(void) {
    HBI_CHECK_EQ_INT(hbi_json_selftest(), HBI_OK);
}

int main(void) {
    HBI_TEST_BEGIN("json");
    HBI_RUN(test_scalars);
    HBI_RUN(test_number_forms);
    HBI_RUN(test_object);
    HBI_RUN(test_array_nested);
    HBI_RUN(test_string_escapes);
    HBI_RUN(test_empty_containers);
    HBI_RUN(test_malformed);
    HBI_RUN(test_selftest);
    return HBI_TEST_END();
}
