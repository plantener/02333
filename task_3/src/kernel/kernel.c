/*!
 * \file kernel.c
 * \brief
 *  This is the main source code for the kernel.
 */

#include "kernel.h"
#include "threadqueue.h"

/* Note: Look in kernel.h for documentation of global variables and
   functions. */

/* Variables */

union thread
thread_table[MAX_NUMBER_OF_THREADS];

struct process
process_table[MAX_NUMBER_OF_PROCESSES];

struct thread_queue
ready_queue;

struct executable
executable_table[MAX_NUMBER_OF_PROCESSES];

int
executable_table_size;

/* The following four variables are set by the assembly code. */
unsigned long first_available_memory_byte;

unsigned long memory_size;

const struct executable_image* ELF_images_start;

const char* ELF_images_end;
/* Initialize the timer queue to be empty. */
int
timer_queue_head=-1;

/* Initialize the system time to be 0. */
long
system_time=0;

/* Function definitions */

void
kprints(const char* string)
{
 /* Loop until we have found the null character. */
 while(1)
 {
  register const char curr = *string++;

  if (curr)
  {
   outb(0xe9, curr);
  }
  else
  {
   return;
  }
 }
}

void
kprinthex(const register long value)
{
 const static char hex_helper[16]="0123456789abcdef";
 register int      i;

 /* Print each character of the hexadecimal number. This is a very inefficient
    way of printing hexadecimal numbers. It is, however, very compact in terms
    of the number of source code lines. */
 for(i=15; i>=0; i--)
 {
  outb(0xe9, hex_helper[(value>>(i*4))&15]);
 }
}

struct prepare_process_return_value
prepare_process(const struct Elf64_Ehdr* elf_image,
                const unsigned int       process,
                unsigned long            memory_footprint_size)
{
 /* Get the address of the program header table. */
 int                program_header_index;
 struct Elf64_Phdr* program_header = ((struct Elf64_Phdr*)
                                       (((char*) (elf_image)) +
                                        elf_image->e_phoff));
 unsigned long      used_memory = 0;
 unsigned long      address_of_first_instruction = 0;
 struct prepare_process_return_value ret_val = {0, 0};

 /* First check that we have enough memory. */
 if (first_available_memory_byte + memory_footprint_size >= memory_size)
 {
  /* No, we don't. */
  return ret_val;
 }

 /* Scan through the program header table and copy all PT_LOAD segments to
    memory. Perform checks at the same time.*/

 for (program_header_index = 0;
      program_header_index < elf_image->e_phnum;
      program_header_index++)
 {
  if (PT_LOAD == program_header[program_header_index].p_type)
  {
   /* Calculate destination adress. */
   unsigned long* dst = (unsigned long *) (first_available_memory_byte + 
                                           used_memory);

   /* Check for odd things. */
   if (
       /* Check if the segment is contigous */
       (used_memory != program_header[program_header_index].p_vaddr) ||
       /* Check if the segmen fits in memory. */
       (used_memory + program_header[program_header_index].p_memsz >
        memory_footprint_size) ||
       /* Check if the segment has an odd size. We require the segement
          size to be an even multiple of 8. */
       (0 != (program_header[program_header_index].p_memsz&7)) ||
       (0 != (program_header[program_header_index].p_filesz&7)))
   {
    /* Something went wrong. Return an error. */
    return ret_val;
   }

   /* First copy p_filesz from the image to memory. */
   {
    /* Calculate the source address. */
    unsigned long* src = (unsigned long *) (((char*) elf_image)+
     program_header[program_header_index].p_offset);
    unsigned long count = program_header[program_header_index].p_filesz/8;

    for(; count>0; count--)
    {
     *dst++=*src++;
    }
   }


   /* Then write p_memsz-p_filesz bytes of zeros. This to pad the segment. */
   {
    unsigned long count = (program_header[program_header_index].p_memsz-
                           program_header[program_header_index].p_filesz)/8;

    for(; count>0; count--)
    {
     *dst++=0;
    }
   }

   /* Finally update the amount of used memory. */
   used_memory += program_header[program_header_index].p_memsz;
  }
 }

 /* Find out the address to the first instruction to be executed. */
 ret_val.first_instruction_address = first_available_memory_byte +
                                     elf_image->e_entry;

 /* Claim the memory. */
 first_available_memory_byte += memory_footprint_size;
 /* And round to nearest higher multiple of 4096 */
 first_available_memory_byte += 4096-1;
 first_available_memory_byte &= -4096;

 return ret_val;
}

void
cleanup_process(const int process)
{
}

void
initialize(void)
{
 register int i;

 /* Loop over all threads in the thread table and reset the owner. */
 for(i=0; i<MAX_NUMBER_OF_THREADS; i++)
 {
  thread_table[i].data.owner=-1; /* -1 is an illegal process_table index.
                                     We use that to show that the thread
                                     is dormant. */
 }

 /* Loop over all processes in the thread table and mark them as not
    executing. */
 for(i=0; i<MAX_NUMBER_OF_PROCESSES; i++)
 {
  process_table[i].threads=0;    /* No executing process has less than 1
                                    thread. */
 }

 /* Initialize the ready queue. */
 thread_queue_init(&ready_queue);

 /* Go through the linked list of executable images and verify that they
    are correct. At the same time build the executable_table. */
 {
  const struct executable_image* image;

  for (image=ELF_images_start; 0!=image; image=image->next)
  {
   unsigned long      image_size;

   /* First calculate the size of the image. */
   if (0 != image->next)
   {
    image_size = ((char *) (image->next)) - ((char *) image) -1;
   }
   else
   {
    image_size = ((char *) ELF_images_end) - ((char *) image) - 1;
   }

   /* Check that the image is an ELF image and that it is of the
      right type. */
   if (
       /* EI_MAG0 - EI_MAG3 have to be 0x7f 'E' 'L' 'F'. */
       (image->elf_image.e_ident[EI_MAG0] != 0x7f) ||
       (image->elf_image.e_ident[EI_MAG1] != 'E') ||
       (image->elf_image.e_ident[EI_MAG2] != 'L') ||
       (image->elf_image.e_ident[EI_MAG3] != 'F') ||
       /* Check that the image is a 64-bit image. */
       (image->elf_image.e_ident[EI_CLASS] != 2) ||
       /* Check that the image is a little endian image. */
       (image->elf_image.e_ident[EI_DATA] != 1) ||
       /* And that the version of the image format is correct. */
       (image->elf_image.e_ident[EI_VERSION] != 1) ||
       /* NB: We do not check the ABI or ABI version. We really should
          but currently those fields are not set properly by the build
          tools. They are both set to zero which means: System V ABI,
          third edition. However, the ABI used is clearly not System V :-) */

       /* Check that the image is executable. */
       (image->elf_image.e_type != 2) ||
       /* Check that the image is executable on AMD64. */
       (image->elf_image.e_machine != 0x3e) ||
       /* Check that the object format is correct. */
       (image->elf_image.e_version != 1) ||
       /* Check that the processor dependent flags are all reset. */
       (image->elf_image.e_flags != 0) ||
       /* Check that the length of the header is what we expect. */
       (image->elf_image.e_ehsize != sizeof(struct Elf64_Ehdr)) ||
       /* Check that the size of the program header table entry is what
          we expect. */
       (image->elf_image.e_phentsize != sizeof(struct Elf64_Phdr)) ||
       /* Check that the number of entries is reasonable. */
       (image->elf_image.e_phnum < 0) ||
       (image->elf_image.e_phnum > 8) ||
       /* Check that the entry point is within the image. */
       (image->elf_image.e_entry < 0) ||
       (image->elf_image.e_entry >= image_size) ||
       /* Finally, check that the program header table is within the image. */
       (image->elf_image.e_phoff > image_size) ||
       ((image->elf_image.e_phoff +
         image->elf_image.e_phnum * sizeof(struct Elf64_Phdr)) > image_size )
      )

   {
    /* There is something wrong with the image. */
    while (1)
    {
     kprints("Kernel panic! Corrupt executable image.\n");
    }
    continue;
   }

   /* Now check the program header table. */
   {
    int                program_header_index;
    struct Elf64_Phdr* program_header = ((struct Elf64_Phdr*)
                                         (((char*) &(image->elf_image)) +
                                          image->elf_image.e_phoff));
    unsigned long      memory_footprint_size = 0;

    for (program_header_index = 0;
         program_header_index < image->elf_image.e_phnum;
         program_header_index++)
    {
     /* First sanity check the entry. */
     if (
         /* Check that the segment is a type we can handle. */
         (program_header[program_header_index].p_type < 0) ||
         (!((program_header[program_header_index].p_type == PT_NULL) ||
            (program_header[program_header_index].p_type == PT_LOAD) ||
            (program_header[program_header_index].p_type == PT_PHDR))) ||
         /* Look more carefully into loadable segments. */
         ((program_header[program_header_index].p_type == PT_LOAD) &&
           /* Check if any flags that we can not handle is set. */
          (((program_header[program_header_index].p_flags & ~7) != 0) ||
           /* Check if sizes and offsets look sane. */
           (program_header[program_header_index].p_offset < 0) ||
           (program_header[program_header_index].p_vaddr < 0) ||
           (program_header[program_header_index].p_filesz < 0) ||
           (program_header[program_header_index].p_memsz < 0) ||
          /* Check if the segment has an odd size. We require the
             segment size to be an even multiple of 8. */
           (0 != (program_header[program_header_index].p_memsz&7)) ||
           (0 != (program_header[program_header_index].p_filesz&7)) ||
           /* Check if the segment goes beyond the image. */
           ((program_header[program_header_index].p_offset +
             program_header[program_header_index].p_filesz) > image_size)))
        )
     {
      while (1)
      {
       kprints("Kernel panic! Corrupt segment.\n");
      }
     }

     /* Check that all PT_LOAD segments are contiguous starting from
        address 0. Also, calculate the memory footprint of the image. */
     if (program_header[program_header_index].p_type == PT_LOAD)
     {
      if (program_header[program_header_index].p_vaddr !=
          memory_footprint_size)
      {
       while (1)
       {
        kprints("Kernel panic! Executable image has illegal memory layout.\n");
       }
      }

      memory_footprint_size += program_header[program_header_index].p_memsz;
     }
    }

    executable_table[executable_table_size].memory_footprint_size =
     memory_footprint_size;
   }

   executable_table[executable_table_size].elf_image = &(image->elf_image);
   executable_table_size += 1;

   kprints("Found an executable image.\n");

   if (executable_table_size >= MAX_NUMBER_OF_PROCESSES)
   {
    while (1)
    {
     kprints("Kernel panic! Too many executable images found.\n");
    }
   }
  }
 }

 /* Check that actually some executable files are found. Also check that the
    thread structure is of the right size. The assembly code will break if it
    is not. */

 if ((0 >= executable_table_size) || (1024 != sizeof(union thread)))
 {
  while (1)
  {
   kprints("Kernel panic! Can not boot.\n");
  }
 }

 /* Start running the first program in the executable table. */

 /* Use the ELF program header table and copy the right portions of the
    image to memory. This is done by prepare_process. */
 {
  struct prepare_process_return_value prepare_process_ret_val = 
   prepare_process(executable_table[0].elf_image,
                   0,
                   executable_table[0].memory_footprint_size);

  if (0 == prepare_process_ret_val.first_instruction_address)
  {
   while (1)
   {
    kprints("Kernel panic! Can not start process 0!\n");
   }
  }

  /* Start executable program 0 as process 0. At this point, there are no
     processes so we can just grab entry 0 and use it. */
  process_table[0].parent=-1;    /* We put -1 to indicate that there is no
                                    parent process. */
  process_table[0].threads=1;

  /* We need a thread. We just take the first one as no threads are running or
     have been allocated at this point. */
  thread_table[0].data.owner=0;  /* 0 is the index of the first process. */

  /* We reset all flags and enable interrupts */
  thread_table[0].data.registers.integer_registers.rflags=0x200;

  /* And set the start address. */
  thread_table[0].data.registers.integer_registers.rip =
   prepare_process_ret_val.first_instruction_address;

  /* Finally we set the current thread. */
  cpu_private_data.thread_index = 0;
 }

 /* Set up the timer hardware to generate interrupts 200 times a second. */
 outb(0x43, 0x36);
 outb(0x40, 78);
 outb(0x40, 23);

 /* Now we set up the interrupt controller to allow timer interrupts. */
 outb(0x20, 0x11);
 outb(0xA0, 0x11);

 outb(0x21, 0x20);
 outb(0xA1, 0x28);

 outb(0x21, 1<<2);
 outb(0xA1, 2);

 outb(0x21, 1);
 outb(0xA1, 1);

 outb(0x21, 0xfe);
 outb(0xA1, 0xff);

 kprints("\n\n\nThe kernel has booted!\n\n\n");
 /* Now go back to assembly language code and let the process run. */
}

int
allocate_thread(void)
{
 register int i;
 /* loop over all threads and find a free thread. */
 for(i=0; i<MAX_NUMBER_OF_THREADS; i++)
 {
  /* An owner index of -1 means that the thread is available. */
  if (-1 == thread_table[i].data.owner)
  {
   return i;
  }
 }

 /* We return -1 to indicate that there are no available threads. */
 return -1;
}

extern void
system_call_handler(void)
{
 register int schedule = 0;

 /* Reset the interrupt flag indicating that the context of the caller was
    saved by the system call routine. */
 thread_table[cpu_private_data.thread_index].data.registers.from_interrupt=0;

 switch(SYSCALL_ARGUMENTS.rax)
 {
  case SYSCALL_PAUSE:
  {
   register int  tmp_thread_index;
   unsigned long timer_ticks=SYSCALL_ARGUMENTS.rdi;

   /* Set the return value before doing anything else. We will switch to a new
      thread very soon! */
   SYSCALL_ARGUMENTS.rax=ALL_OK;

   if (0 == timer_ticks)
   {
    /* We should not wait if we are asked to wait for less then one tick. */
    break;
   }

   /* Get the current thread. */
   tmp_thread_index=cpu_private_data.thread_index;

   /* Force a re-schedule. */
   schedule=1;

   /* And insert the thread into the timer queue. */

   /* The timer queue is a linked list of threads. The head (first entry)
      (thread) in the list has a list_data field that holds the number of
      ticks to wait before the thread is made ready. The next entries (threads)
      has a list_data field that holds the number of ticks to wait after the
      previous thread is made ready. This is called to use a delta-time and
      makes the code to test if threads should be made ready very quick. It
      also, unfortunately, makes the code that insert code into the queue
      rather complex. */

   /* If the queue is empty put the thread as only entry. */
   if (-1 == timer_queue_head)
   {
    thread_table[tmp_thread_index].data.next=-1;
    thread_table[tmp_thread_index].data.list_data=timer_ticks;
    timer_queue_head=tmp_thread_index;
   }
   else
   {
    /* Check if the thread should be made ready before the head of the
       previous timer queue. */
    register int curr_timer_queue_entry=timer_queue_head;

    if (thread_table[curr_timer_queue_entry].data.list_data>timer_ticks)
    {
     /* If so set it up as the head in the new timer queue. */

     thread_table[curr_timer_queue_entry].data.list_data-=timer_ticks;
     thread_table[tmp_thread_index].data.next=curr_timer_queue_entry;
     thread_table[tmp_thread_index].data.list_data=timer_ticks;
     timer_queue_head=tmp_thread_index;
    }
    else
    {
     register int prev_timer_queue_entry = curr_timer_queue_entry;

     /* Search until the end of the queue or until we found the right spot. */
     while((-1 != thread_table[curr_timer_queue_entry].data.next) &&
           (timer_ticks>=thread_table[curr_timer_queue_entry].data.list_data))
     {
      timer_ticks-=thread_table[curr_timer_queue_entry].data.list_data;
      prev_timer_queue_entry=curr_timer_queue_entry;
      curr_timer_queue_entry=thread_table[curr_timer_queue_entry].data.next;
     }


     if (timer_ticks>=thread_table[curr_timer_queue_entry].data.list_data)
     {
      /* Insert the thread into the queue after the existing entry. */
      thread_table[tmp_thread_index].data.next=
       thread_table[curr_timer_queue_entry].data.next;
      thread_table[curr_timer_queue_entry].data.next=tmp_thread_index;
      thread_table[tmp_thread_index].data.list_data=timer_ticks-
       thread_table[curr_timer_queue_entry].data.list_data;
     }
     else
     {
      /* Insert the thread into the queue before the existing entry. */
      thread_table[tmp_thread_index].data.next=
       curr_timer_queue_entry;
      thread_table[prev_timer_queue_entry].data.next=tmp_thread_index;
      thread_table[tmp_thread_index].data.list_data=timer_ticks;
      thread_table[curr_timer_queue_entry].data.list_data-=timer_ticks;
     }
    }
   }
   break;
  }

  case SYSCALL_TIME:
  {
   /* Returns the current system time to the program. */
   SYSCALL_ARGUMENTS.rax=system_time;
   break;
  }

  default:
  {
   schedule = system_call_implementation();
   break;
  }
 }

 scheduler_called_from_system_call_handler(schedule);
}

extern void
timer_interrupt_handler(void)
{
 register int thread_changed=0;
 /*!< Interrupt hander code may set this variable to 0. The variable is
      used as input to the scheduler to indicate if the interrupt code has
      updated scheduling data structures. */

 /* Increment system time. */
 system_time++;

 /* Check if there are any thread that we should make ready.
    First check if there are any threads at all in the timer
    queue. */
 if (-1 != timer_queue_head)
 {
  /* Then decrement the list_data in the head. */
  thread_table[timer_queue_head].data.list_data-=1;

  /* Then remove all elements including with a list_data equal to zero
     and insert them into the ready queue. These are the threads that
     should be woken up. */
  while((-1 != timer_queue_head) &&
        /* We remove all entries less than or equal to 0. Equality should be
           enough but checking with less than or equal may hide the symptoms
           of some bugs and make the system more stable. */
        (thread_table[timer_queue_head].data.list_data<=0))
  {
   register int tmp_thread_index=timer_queue_head;
   /* Remove the head element.*/
   timer_queue_head=thread_table[tmp_thread_index].data.next;

   /* Let the woken thread run if the CPU is not running any thread. */
   if (-1 == cpu_private_data.thread_index)
   {
    cpu_private_data.thread_index = tmp_thread_index;
    thread_changed=1;
   }
   else
   {
    /* Or insert it into the ready queue. */
    thread_queue_enqueue(&ready_queue, tmp_thread_index);
   }
  }
 }

 scheduler_called_from_timer_interrupt_handler(thread_changed);

 /* Acknowledge interrupt so that new interrupts can be sent to the CPU. */
 outb(0x20, 0x20);
}
