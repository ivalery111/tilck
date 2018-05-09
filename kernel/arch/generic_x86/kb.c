

/*
 * This is a DEMO/DEBUG version of the KB driver.
 * Until the concept of character devices is implemented in exOS, that's
 * good enough for basic experiments.
 *
 * Useful info:
 * http://wiki.osdev.org/PS/2_Keyboard
 * http://www.brokenthorn.com/Resources/OSDev19.html
 *
 */


#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/term.h>
#include <exos/tasklet.h>
#include <exos/hal.h>
#include <exos/process.h>
#include <exos/sync.h>
#include <exos/ringbuf.h>
#include <exos/timer.h>

#define KB_CBUF_SIZE 256

static char kb_cooked_buf[256];
static ringbuf kb_cooked_ringbuf;

bool kb_cbuf_is_empty(void)
{
   return ringbuf_is_empty(&kb_cooked_ringbuf);
}

bool kb_cbuf_is_full(void)
{
   return ringbuf_is_full(&kb_cooked_ringbuf);
}

char kb_cbuf_read_elem(void)
{
   char ret;
   ASSERT(!kb_cbuf_is_empty());
   DEBUG_CHECKED_SUCCESS(ringbuf_read_elem1(&kb_cooked_ringbuf, &ret));
   return ret;
}

static ALWAYS_INLINE bool kb_cbuf_drop_last_written_elem(void)
{
   return ringbuf_unwrite_elem(&kb_cooked_ringbuf);
}

static ALWAYS_INLINE bool kb_cbuf_write_elem(char c)
{
   return ringbuf_write_elem1(&kb_cooked_ringbuf, c);
}

#define KB_DATA_PORT 0x60
#define KB_CONTROL_PORT 0x64

/* keyboard interface bits */
#define KBRD_BIT_KDATA 0     // keyboard data is in buffer
                             // (output buffer is empty) (bit 0)

#define KBRD_BIT_UDATA 1     // user data is in buffer
                             // (command buffer is empty) (bit 1)

#define KBRD_RESET 0xFE /* reset CPU command */

#define KB_ACK 0xFA
#define KB_RESEND 0xFE

#define BIT(n) (1 << (n))
#define CHECK_FLAG(flags, n) ((flags) & BIT(n))

/* US Keyboard Layout.  */
unsigned char kbdus[128] =
{
   0,  27, '1', '2', '3', '4', '5', '6', '7', '8',
   '9', '0', '-', '=', '\b',  /* Backspace */
   '\t',       /* Tab */
   'q', 'w', 'e', 'r',  /* 19 */
   't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
   0,       /* 29   - Control */
   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  /* 39 */
   '\'', '`',
   0,      /* Left shift */
   '\\', 'z', 'x', 'c', 'v', 'b', 'n',       /* 49 */
   'm', ',', '.', '/',
   0,            /* Right shift */
   '*',
   0, /* Alt */
   ' ',  /* Space bar */
   0, /* Caps lock */
   0, /* 59 - F1 key ... > */
   0,   0,   0,   0,   0,   0,   0,   0,
   0, /* < ... F10 */
   0, /* 69 - Num lock*/
   0, /* Scroll Lock */
   0, /* Home key */
   0, /* Up Arrow */
   0, /* Page Up */
   '-',
   0, /* Left Arrow */
   0,
   0, /* Right Arrow */
   '+',
   0, /* 79 - End key*/
   0, /* Down Arrow */
   0, /* Page Down */
   0, /* Insert Key */
   0, /* Delete Key */
   0,   0,   '\\',
   0, /* F11 Key */
   0, /* F12 Key */
   0, /* All other keys are undefined */
};

unsigned char kbdus_up[128] =
{
   0,  27, '!', '@', '#', '$', '%', '^', '&', '*',
   '(', ')', '_', '+', '\b',
   '\t',       /* Tab */
   'Q', 'W', 'E', 'R',  /* 19 */
   'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
   0,       /* 29   - Control */
   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',  /* 39 */
   '\"', '~',
   0,      /* Left shift */
   '|', 'Z', 'X', 'C', 'V', 'B', 'N',       /* 49 */
   'M', '<', '>', '?',
   0,            /* Right shift */
   '*',
   0, /* Alt */
   ' ',  /* Space bar */
   0, /* Caps lock */
   0, /* 59 - F1 key ... > */
   0,   0,   0,   0,   0,   0,   0,   0,
   0, /* < ... F10 */
   0, /* 69 - Num lock*/
   0, /* Scroll Lock */
   0, /* Home key */
   0, /* Up Arrow */
   0, /* Page Up */
   '-',
   0, /* Left Arrow */
   0,
   0, /* Right Arrow */
   '+',
   0, /* 79 - End key*/
   0, /* Down Arrow */
   0, /* Page Down */
   0, /* Insert Key */
   0, /* Delete Key */
   0,   0,  '|',
   0, /* F11 Key */
   0, /* F12 Key */
   0, /* All other keys are undefined */
};

unsigned char *us_kb_layouts[2] = {
   kbdus, kbdus_up
};

u8 numkey[128] = {
   [71] = '7', '8', '9',
   [75] = '4', '5', '6',
   [79] = '1', '2', '3',
   [82] = '0', '.'
};

#define KEY_L_SHIFT 42
#define KEY_R_SHIFT 54
#define NUM_LOCK 69
#define CAPS_LOCK 58
#define KEY_PAGE_UP 0x49
#define KEY_PAGE_DOWN 0x51
#define KEY_LEFT 0x4b
#define KEY_RIGHT 0x4d
#define KEY_UP 0x48
#define KEY_DOWN 0x50

#define KEY_CTRL 0x1d
#define KEY_ALT 0x38
#define KEY_E0_DEL 0x53

bool pkeys[128];
bool e0pkeys[128];

bool *pkeysArrays[2] = { pkeys, e0pkeys };

bool numLock = false;
bool capsLock = false;
bool lastWasE0 = false;

u8 next_kb_interrupts_to_ignore = 0;

void kbd_wait()
{
   u8 temp;

   /* Clear all keyboard buffers (output and command buffers) */
   do
   {
      temp = inb(KB_CONTROL_PORT); /* empty user data */
      if (CHECK_FLAG(temp, KBRD_BIT_KDATA) != 0) {
         inb(KB_DATA_PORT); /* empty keyboard data */
      }
   } while (CHECK_FLAG(temp, KBRD_BIT_UDATA) != 0);
}

void kb_led_set(u8 val)
{
   kbd_wait();

   outb(KB_DATA_PORT, 0xED);
   kbd_wait();

   outb(KB_DATA_PORT, val & 7);
   kbd_wait();
}

void num_lock_switch(bool val)
{
   kb_led_set(capsLock << 2 | val << 1);
}

void caps_lock_switch(bool val)
{
   kb_led_set(numLock << 1 | val << 2);
}

/*
 * Condition variable on which tasks interested in keyboard input, wait.
 */
kcond kb_cond;

#ifdef DEBUG

static int chars_count = 0;
static u64 total_cycles = 0;

#endif

extern u32 spur_irq_count;

void handle_key_pressed(u8 scancode)
{
   switch(scancode) {

   case NUM_LOCK:
      numLock = !numLock;
      num_lock_switch(numLock);
      printk("\nNUM LOCK is %s\n", numLock ? "ON" : "OFF");
      return;

   case CAPS_LOCK:
      capsLock = !capsLock;
      caps_lock_switch(capsLock);
      printk("\nCAPS LOCK is %s\n", capsLock ? "ON" : "OFF");
      return;

   case KEY_L_SHIFT:
   case KEY_R_SHIFT:
      return;

   default:
      break;
   }

   u8 *layout = us_kb_layouts[pkeys[KEY_L_SHIFT] || pkeys[KEY_R_SHIFT]];
   u8 c = layout[scancode];

   if (numLock) {
      c |= numkey[scancode];
   }

   if (capsLock) {
      c = toupper(c);
   }

   if (!c) {
      printk("PRESSED scancode: 0x%x (%i)\n", scancode, scancode);
      return;
   }

   // debug way to trigger a panic
   // if (c == '$')
   //    NOT_REACHED();

#ifdef DEBUG

   if (c == '!') {
      printk("Spurious IRQ count: %u (%u / sec)\n",
             spur_irq_count, spur_irq_count / (jiffies / TIMER_HZ));
      return;
   }

   if (c == '@') {
      printk("\nkey press int handler avg. cycles = %llu [%i samples]\n",
             total_cycles/chars_count, chars_count);
      return;
   }

   u64 start, end;
   start = RDTSC();

#endif

   if (c != '\b') {

      if (kb_cbuf_write_elem(c)) {
         term_write_char(c);
         if (c == '\n' || kb_cbuf_is_full()) {
            bool success = enqueue_tasklet1(kcond_signal_one, &kb_cond);
            VERIFY(success); // TODO: any better way to handle this?
         }
      }

   } else {
      if (kb_cbuf_drop_last_written_elem())
         term_write_char(c);
   }

#ifdef DEBUG
   end = RDTSC();
   chars_count++;
   total_cycles += (end - start);
#endif
}

void handle_E0_key_pressed(u8 scancode)
{
   switch (scancode) {

   case KEY_PAGE_UP:
      term_scroll_up(5);
      break;

   case KEY_PAGE_DOWN:
      term_scroll_down(5);
      break;

   case KEY_E0_DEL:

      if (pkeysArrays[0][KEY_CTRL] && pkeysArrays[0][KEY_ALT]) {
         printk("Ctrl + Alt + Del: Reboot!\n");
         reboot();
         break;
      }

   // fall-through

   default:
      printk("PRESSED E0 scancode: 0x%x (%i)\n", scancode, scancode);
      break;
   }
}

void (*keyPressHandlers[2])(u8) = {
   handle_key_pressed, handle_E0_key_pressed
};



void keyboard_handler()
{
   u8 scancode;
   ASSERT(are_interrupts_enabled());

   while (inb(KB_CONTROL_PORT) & 2) {
      //check if scancode is ready
      //this is useful since sometimes the IRQ is triggered before
      //the data is available.
   }

   /* Read from the keyboard's data buffer */
   scancode = inb(KB_DATA_PORT);

   // Hack used to avoid handling 0xE1 two-scancode sequences
   if (next_kb_interrupts_to_ignore) {
      next_kb_interrupts_to_ignore--;
      return;
   }

   // Hack used to avoid handling 0xE1 two-scancode sequences
   if (scancode == 0xE1) {
      next_kb_interrupts_to_ignore = 2;
      return;
   }

   if (scancode == 0xE0) {
      lastWasE0 = true;
      return;
   }


   if (lastWasE0) {
      // Fake lshift pressed (2A) or released (AA)
      if (scancode == 0x2A || scancode == 0xAA) {
         goto end;
      }
   }

   int is_pressed = !(scancode & 0x80);
   scancode &= ~0x80;

   pkeysArrays[lastWasE0][scancode] = is_pressed;

   if (is_pressed)
      keyPressHandlers[lastWasE0](scancode);

end:
   lastWasE0 = false;
}

// Reboot procedure using the keyboard controller

void reboot() {

   disable_interrupts_forced();
   kbd_wait();

   outb(KB_CONTROL_PORT, KBRD_RESET);

   while (true) {
      halt();
   }
}


/*
 * From http://wiki.osdev.org/PS/2_Keyboard
 *
 * BITS [0,4]: Repeat rate (00000b = 30 Hz, ..., 11111b = 2 Hz)
 * BITS [5,6]: Delay before keys repeat (00b = 250 ms, ..., 11b = 1000 ms)
 * BIT [7]: Must be zero
 * BIT [8]: I'm assuming is ignored.
 *
 */

void kb_set_typematic_byte(u8 val)
{
   kbd_wait();
   outb(KB_DATA_PORT, 0xF3);
   kbd_wait();
   outb(KB_DATA_PORT, 0);
   kbd_wait();
}

/* This will be executed in a tasklet */
void init_kb()
{
   disable_preemption();

   ringbuf_init(&kb_cooked_ringbuf, KB_CBUF_SIZE, 1, kb_cooked_buf);
   kcond_init(&kb_cond);

   num_lock_switch(numLock);
   caps_lock_switch(capsLock);
   kb_set_typematic_byte(0);

   printk("keyboard initialized.\n");

   enable_preemption();
}
