/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_modules.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/term.h>
#include <tilck/mods/fb_console.h>
#include <tilck/kernel/cmdline.h>

#include "termutil.h"

#define DUMP_LABEL(name)     dp_write(row++, col, "%-16s", name)
#define DUMP_INT(name, val)  dp_write(row++, col, "  %-16s: %d", name, val)
#define DUMP_STR_OPT(opt)    dp_write(row++, col, "  %-30s: %s", #opt, opt)
#define DUMP_INT_OPT(opt)    dp_write(row++, col, "  %-30s: %d", #opt, opt)
#define DUMP_BOOL_OPT(opt)                               \
   dp_write(row++,                                       \
            col,                                         \
            "  %-30s: %s%u" RESET_ATTRS,                 \
            #opt,                                        \
            opt ? ESC_COLOR_GREEN : DP_ESC_COLOR,        \
            opt)

static void dp_show_opts(void)
{
   int row = dp_screen_start_row + 1;
   int col = dp_start_col + 3;
   int rows_left;
   int rows_right;
   int max_rows;

   DUMP_LABEL("Main");
   DUMP_INT_OPT(IS_RELEASE_BUILD);
   DUMP_STR_OPT(BUILDTYPE_STR);
   DUMP_INT_OPT(TIMER_HZ);
   DUMP_INT_OPT(USER_STACK_PAGES);

   DUMP_LABEL("Kernel modules");
   DUMP_BOOL_OPT(MOD_tty);
   DUMP_BOOL_OPT(MOD_fb);
   DUMP_BOOL_OPT(MOD_serial);
   DUMP_BOOL_OPT(MOD_debugpanel);

   DUMP_LABEL("Modules config");
   DUMP_BOOL_OPT(KERNEL_FB_BANNER);
   DUMP_BOOL_OPT(KERNEL_SHOW_LOGO);
   DUMP_BOOL_OPT(FB_CONSOLE_USE_ALT_FONTS);

   DUMP_LABEL("Enabled by default");
   DUMP_BOOL_OPT(KRN_TRACK_NESTED_INTERR);
   DUMP_BOOL_OPT(PANIC_SHOW_STACKTRACE);
   DUMP_BOOL_OPT(DEBUG_CHECKS_IN_RELEASE);
   DUMP_BOOL_OPT(KERNEL_SELFTESTS);
   DUMP_BOOL_OPT(KERNEL_STACK_ISOLATION);

   DUMP_LABEL("Disabled by default");
   DUMP_BOOL_OPT(KERNEL_BIG_IO_BUF);
   DUMP_BOOL_OPT(KERNEL_DO_PS2_SELFTEST);
   DUMP_BOOL_OPT(KERNEL_GCOV);
   DUMP_BOOL_OPT(FORK_NO_COW);
   DUMP_BOOL_OPT(MMAP_NO_COW);
   DUMP_BOOL_OPT(PANIC_SHOW_REGS);
   DUMP_BOOL_OPT(KMALLOC_HEAVY_STATS);
   DUMP_BOOL_OPT(KMALLOC_FREE_MEM_POISONING);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_DEBUG_LOG);
   DUMP_BOOL_OPT(KMALLOC_SUPPORT_LEAK_DETECTOR);
   DUMP_BOOL_OPT(BOOTLOADER_POISON_MEMORY);

   rows_left = row - dp_screen_start_row - 1;
   row = dp_screen_start_row+1;
   col = dp_start_col + 48;

   DUMP_LABEL("Main");
   DUMP_INT("HYPERVISOR", in_hypervisor());

   DUMP_LABEL("Console");
   DUMP_INT("TERM_ROWS", term_get_rows(get_curr_term()));
   DUMP_INT("TERM_COLS", term_get_cols(get_curr_term()));
   DUMP_INT("USE_FRAMEBUFFER", use_framebuffer());

   if (MOD_fb && use_framebuffer()) {
      struct fb_console_info fbi;
      fb_console_get_info(&fbi);

      DUMP_INT("FB_OPT_FUNCS", fb_is_using_opt_funcs());
      DUMP_INT("FB_RES_X", fbi.res_x);
      DUMP_INT("FB_RES_Y", fbi.res_y);
      DUMP_INT("FB_BBP", fbi.bpp);
      DUMP_INT("FB_FONT_W", fbi.font_w);
      DUMP_INT("FB_FONT_H", fbi.font_h);
   }

   DUMP_INT("TTY_COUNT", kopt_tty_count);

   rows_right = row - dp_screen_start_row - 1;
   max_rows = MAX(rows_left, rows_right);

   /* left rectangle */
   dp_draw_rect("Build-time",
                dp_screen_start_row, dp_start_col + 1, max_rows+2, 45);

   /* right rectangle */
   dp_draw_rect("Boot-time",
                dp_screen_start_row, col - 2, max_rows+2, 29);
}

static struct dp_screen dp_opts_screen =
{
   .index = 0,
   .label = "Options",
   .draw_func = dp_show_opts,
   .on_keypress_func = NULL,
};

__attribute__((constructor))
static void dp_opts_init(void)
{
   dp_register_screen(&dp_opts_screen);
}
