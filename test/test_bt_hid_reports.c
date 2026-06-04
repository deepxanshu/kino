#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../main/bluetooth/bt_hid_mouse.h"

static void test_key_report_builds_f15_down_and_release_payload(void)
{
    uint8_t report[BT_HID_KEY_REPORT_SIZE] = {0xff};

    bt_hid_f15_report_build(report, true);
    assert(report[0] == BT_HID_F15_USAGE);

    bt_hid_f15_report_build(report, false);
    assert(report[0] == 0x00);
}

int main(void)
{
    test_key_report_builds_f15_down_and_release_payload();
    return 0;
}
