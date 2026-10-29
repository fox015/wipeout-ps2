#ifndef TODO_PS2_H
#define TODO_PS2_H

// Marks a PS2 function that is still a stub. Logs "TODO: <function>" the first
// time each one is called, so the PCSX2 log shows what the game still needs.

void ps2_log(const char *msg);
void ps2_todo(const char *func);

#define TODO() do { \
	static int todo_logged; \
	if (!todo_logged) { todo_logged = 1; ps2_todo(__func__); } \
} while (0)

#endif
