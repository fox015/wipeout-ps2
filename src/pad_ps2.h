#ifndef PAD_PS2_H
#define PAD_PS2_H

// Load the pad IOP modules and open port 1.
void pad_init(void);

// Read the pad and pass its state to input.c. Call once per frame.
void pad_update(void);

#endif
