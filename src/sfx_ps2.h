#ifndef SFX_PS2_H
#define SFX_PS2_H

// Once a frame: send the sound commands queued for the IOP (see sfx_ps2.c).
void sfx_ps2_frame_end(void);

#endif
