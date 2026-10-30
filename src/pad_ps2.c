// PS2 pad input: port 1 through libpad and the BIOS's SIO2MAN/PADMAN modules.
//
// Buttons map onto the Dreamcast layout the game was tuned for, by position:
// Cross = A, Circle = B, Square = X, Triangle = Y, L2/R2 = the triggers
// (airbrakes by default). L1/R1 are the shoulder buttons, free to remap.

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>

#include "input.h"
#include "utils.h"
#include "pad_ps2.h"

// Analog sticks are 0-255 with 128 at rest; ignore drift near the centre.
#define STICK_DEADZONE 24

extern int if_to_await;

static char pad_buf[256] __attribute__((aligned(64)));
static int pad_ready;
static int analog_requested;
static uint16_t old_buttons;

void pad_init(void) {
	SifInitRpc(0);

	if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0 ||
		SifLoadModule("rom0:PADMAN", 0, NULL) < 0) {
		printf("pad: could not load SIO2MAN/PADMAN\n");
		return;
	}

	padInit(0);
	if (padPortOpen(0, 0, pad_buf) == 0) {
		printf("pad: could not open port 1\n");
		return;
	}
	pad_ready = 1;
	printf("pad: port 1 open\n");
}

// -1..1 from a 0-255 stick axis, with a deadzone around the centre.
static float stick_axis(unsigned char raw) {
	int v = (int)raw - 128;
	if (v > -STICK_DEADZONE && v < STICK_DEADZONE) {
		return 0;
	}
	return clamp((float)v / 127.0f, -1.0f, 1.0f);
}

static void set_axis(button_t negative, button_t positive, float value) {
	input_set_button_state(negative, value < 0 ? -value : 0);
	input_set_button_state(positive, value > 0 ? value : 0);
}

void pad_update(void) {
	struct padButtonStatus status;
	uint16_t buttons = 0;
	float lx = 0, ly = 0, rx = 0, ry = 0;

	int state = pad_ready ? padGetState(0, 0) : PAD_STATE_DISCONN;
	if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
		// Lock the pad into analog (DualShock) mode once it's ready.
		if (!analog_requested && padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK)) {
			analog_requested = 1;
		}
		if (padRead(0, 0, &status) != 0) {
			buttons = 0xffff ^ status.btns; // pressed buttons read as 0
			if (status.mode >> 4 == 0x7) { // analog mode reports the sticks
				lx = stick_axis(status.ljoy_h);
				ly = stick_axis(status.ljoy_v);
				rx = stick_axis(status.rjoy_h);
				ry = stick_axis(status.rjoy_v);
			}
		}
	}

	uint16_t pressed_now = buttons & ~old_buttons;
	old_buttons = buttons;

	#define BUTTON(mask) ((buttons & (mask)) ? 1.0f : 0.0f)

	// The button remap menu wants Start and Cross only on the frame they go down.
	if (if_to_await) {
		input_set_button_state(INPUT_GAMEPAD_START, (pressed_now & PAD_START) ? 1.0f : 0.0f);
		input_set_button_state(INPUT_GAMEPAD_A, (pressed_now & PAD_CROSS) ? 1.0f : 0.0f);
	} else {
		input_set_button_state(INPUT_GAMEPAD_START, BUTTON(PAD_START));
		input_set_button_state(INPUT_GAMEPAD_A, BUTTON(PAD_CROSS));
	}
	input_set_button_state(INPUT_GAMEPAD_B, BUTTON(PAD_CIRCLE));
	input_set_button_state(INPUT_GAMEPAD_X, BUTTON(PAD_SQUARE));
	input_set_button_state(INPUT_GAMEPAD_Y, BUTTON(PAD_TRIANGLE));
	input_set_button_state(INPUT_GAMEPAD_SELECT, BUTTON(PAD_SELECT));
	input_set_button_state(INPUT_GAMEPAD_L_TRIGGER, BUTTON(PAD_L2));
	input_set_button_state(INPUT_GAMEPAD_R_TRIGGER, BUTTON(PAD_R2));
	input_set_button_state(INPUT_GAMEPAD_L_SHOULDER, BUTTON(PAD_L1));
	input_set_button_state(INPUT_GAMEPAD_R_SHOULDER, BUTTON(PAD_R1));
	input_set_button_state(INPUT_GAMEPAD_L_STICK_PRESS, BUTTON(PAD_L3));
	input_set_button_state(INPUT_GAMEPAD_R_STICK_PRESS, BUTTON(PAD_R3));
	input_set_button_state(INPUT_GAMEPAD_DPAD_UP, BUTTON(PAD_UP));
	input_set_button_state(INPUT_GAMEPAD_DPAD_DOWN, BUTTON(PAD_DOWN));
	input_set_button_state(INPUT_GAMEPAD_DPAD_LEFT, BUTTON(PAD_LEFT));
	input_set_button_state(INPUT_GAMEPAD_DPAD_RIGHT, BUTTON(PAD_RIGHT));

	#undef BUTTON

	set_axis(INPUT_GAMEPAD_L_STICK_LEFT, INPUT_GAMEPAD_L_STICK_RIGHT, lx);
	set_axis(INPUT_GAMEPAD_L_STICK_UP, INPUT_GAMEPAD_L_STICK_DOWN, ly);
	set_axis(INPUT_GAMEPAD_R_STICK_LEFT, INPUT_GAMEPAD_R_STICK_RIGHT, rx);
	set_axis(INPUT_GAMEPAD_R_STICK_UP, INPUT_GAMEPAD_R_STICK_DOWN, ry);
}
