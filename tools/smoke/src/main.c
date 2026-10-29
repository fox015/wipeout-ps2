// Toolchain smoke test: proves the build -> .elf -> PCSX2 -> log loop works.
// tools/build_and_run.sh tools/smoke "SMOKE: ok"

#include <stdio.h>
#include <debug.h>
#include <sio.h>
#include <kernel.h>

int main(int argc, char *argv[])
{
	// SIO goes straight to the serial port, which PCSX2 copies to emuLog.txt.
	sio_init(115200, 0, 0, 0, 0);
	sio_puts("SMOKE: sio_puts reached main\n");

	// Check whether stdio printf also reaches the log (wanted for stage 2 logging).
	printf("SMOKE: printf works\n");

	init_scr();
	scr_printf("wipeout-ps2 smoke test\n");

	sio_puts("SMOKE: ok\n");

	while (1) {
		// Nothing to do; idle so PCSX2 keeps showing the screen.
	}

	return 0;
}
