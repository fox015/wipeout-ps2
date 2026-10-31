#ifndef IOP_PS2_H
#define IOP_PS2_H

// Load the IOP modules for sound: libsd (the SPU2 driver), ps2snd (EE access
// to libsd's voices, sample uploads and reverb) and audsrv (streamed PCM, for
// music). They're built into the ELF. Returns 0 on success.
int iop_load_sound_modules(void);

// Load the BIOS's memory card modules (MCMAN, MCSERV; they need the BIOS's
// SIO2MAN, which pad_init() loads) and start libmc. Returns 0 on success.
int iop_load_memcard_modules(void);

#endif
