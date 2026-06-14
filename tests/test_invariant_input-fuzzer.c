#include <check.h>
#include <stdlib.h>
#include <string.h>

/* Declaration of the fuzzer entry point from fuzz/input-fuzzer.c */
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

START_TEST(test_input_parser_no_oob_on_oversized_input)
{
    /* Invariant: Buffer reads never exceed the declared length;
       oversized inputs must be truncated or rejected without crash. */

    /* 1. Oversized escape sequence (10x typical buffer ~4KB) */
    size_t large_size = 40960;
    unsigned char *large_buf = malloc(large_size);
    ck_assert_ptr_nonnull(large_buf);
    memset(large_buf, 'A', large_size);
    large_buf[0] = '\033';
    large_buf[1] = '[';
    ck_assert_int_eq(LLVMFuzzerTestOneInput(large_buf, large_size), 0);
    free(large_buf);

    /* 2. Boundary: exactly 2x typical param buffer with nested escapes */
    size_t boundary_size = 8192;
    unsigned char *boundary_buf = malloc(boundary_size);
    ck_assert_ptr_nonnull(boundary_buf);
    memset(boundary_buf, ';', boundary_size);
    boundary_buf[0] = '\033';
    boundary_buf[1] = ']';
    boundary_buf[boundary_size - 1] = '\007';
    ck_assert_int_eq(LLVMFuzzerTestOneInput(boundary_buf, boundary_size), 0);
    free(boundary_buf);

    /* 3. Valid short escape sequence (normal case) */
    const unsigned char valid[] = "\033[0;1;32mHello\033[0m";
    ck_assert_int_eq(LLVMFuzzerTestOneInput(valid, sizeof(valid) - 1), 0);

    /* 4. Crafted DCS sequence with excessive length */
    size_t dcs_size = 65536;
    unsigned char *dcs_buf = malloc(dcs_size);
    ck_assert_ptr_nonnull(dcs_buf);
    memset(dcs_buf, 'x', dcs_size);
    dcs_buf[0] = '\033';
    dcs_buf[1] = 'P';
    dcs_buf[dcs_size - 2] = '\033';
    dcs_buf[dcs_size - 1] = '\\';
    ck_assert_int_eq(LLVMFuzzerTestOneInput(dcs_buf, dcs_size), 0);
    free(dcs_buf);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_input_parser_no_oob_on_oversized_input);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}