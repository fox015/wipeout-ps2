#ifndef IOP_PS2_H
#define IOP_PS2_H

// Load the IOP modules for sound: libsd (the SPU2 driver), ps2snd (EE access
// to libsd's voices, sample uploads and reverb) and audsrv (streamed PCM, for
// music). They're built into the ELF. Returns 0 on success.
int iop_load_sound_modules(void);

#endif
