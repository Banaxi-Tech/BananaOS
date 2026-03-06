#ifndef BEX_H
#define BEX_H

#include <stdint.h>

// External assembly syscall wrappers
extern void sys_print(const char* str);
extern void sys_popup(const char* str);
extern void sys_exit();

// GUI System Calls
extern void sys_window_create(int w, int h, const char* title, uint32_t* buffer);
extern void sys_window_update();
extern void sys_get_event(int* mouse_x, int* mouse_y, int* mouse_clicked);

#endif