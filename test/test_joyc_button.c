#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../main/joystick/joyc_button.h"

static void test_joyc_button_is_active_low(void)
{
    assert(joyc_button_decode_pressed(0x01) == false);
    assert(joyc_button_decode_pressed(0xff) == false);
    assert(joyc_button_decode_pressed(0x00) == true);
}

int main(void)
{
    test_joyc_button_is_active_low();
    return 0;
}
