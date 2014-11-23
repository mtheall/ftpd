#pragma once

void console_init(void);
void console_exit(void);

__attribute__((format(printf,1,2)))
void console_set_status(const char *fmt, ...);

__attribute__((format(printf,1,2)))
void console_print(const char *fmt, ...);

void console_render(void);
