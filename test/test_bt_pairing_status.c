#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "bt_pairing_status.h"

static void test_status_text_values(void)
{
    assert(strcmp(bt_pairing_status_text(BT_PAIRING_STATE_DISCOVERABLE), "Discoverable") == 0);
    assert(strcmp(bt_pairing_status_text(BT_PAIRING_STATE_PAIRING), "Pairing") == 0);
    assert(strcmp(bt_pairing_status_text(BT_PAIRING_STATE_PAIRED), "Paired") == 0);
}

static void test_status_resolution_priority(void)
{
    assert(bt_pairing_status_resolve(true, false, false) == BT_PAIRING_STATE_DISCOVERABLE);
    assert(bt_pairing_status_resolve(true, true, false) == BT_PAIRING_STATE_PAIRING);
    assert(bt_pairing_status_resolve(false, true, true) == BT_PAIRING_STATE_PAIRING);
    assert(bt_pairing_status_resolve(false, false, true) == BT_PAIRING_STATE_PAIRED);
    assert(bt_pairing_status_resolve(false, false, false) == BT_PAIRING_STATE_DISCOVERABLE);
}

int main(void)
{
    test_status_text_values();
    test_status_resolution_priority();
    return 0;
}
