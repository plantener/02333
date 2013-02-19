/*! \file syscall.c 

 This files holds the implementations of all system calls.

 */

#include "kernel.h"

int
system_call_implementation(void)
{
 register int schedule=0;
 /*!< System calls may set this variable to 1. The variable is used as
      input to the scheduler to indicate if scheduling is necessary. */
 switch(SYSCALL_ARGUMENTS.rax)
 {
  case SYSCALL_PRINTS:
  {
   kprints((char*) (SYSCALL_ARGUMENTS.rdi));
   SYSCALL_ARGUMENTS.rax = ALL_OK;

   break;
  }

  case SYSCALL_PRINTHEX:
  {
   kprinthex(SYSCALL_ARGUMENTS.rdi);
   SYSCALL_ARGUMENTS.rax = ALL_OK;
   break;
  }

  case SYSCALL_DEBUGGER:
  {
   /* Enable the bochs iodevice and force a return to the debugger. */
   outw(0x8a00, 0x8a00);
   outw(0x8a00, 0x8ae0);

   SYSCALL_ARGUMENTS.rax = ALL_OK;
   break;
  }

	case SYSCALL_VERSION: {
		SYSCALL_ARGUMENTS.rax = KERNEL_VERSION;
		break;
	}

	case SYSCALL_CREATEPROCESS: {

		int process_number, thread_number;
		long int executable_number = SYSCALL_ARGUMENTS.rdi;
		struct prepare_process_return_value prepare_process_ret_val;


		for (process_number = 0; process_number < MAX_NUMBER_OF_PROCESSES && process_table[process_number].threads > 0; process_number++) {
		}
		prepare_process_ret_val = prepare_process(
				executable_table[executable_number].elf_image,
				process_number,
				executable_table[executable_number].memory_footprint_size);

		if(0 == prepare_process_ret_val.first_instruction_address) {
			kprints("Error starting image\n");
		}

		process_table[process_number].parent = thread_table[cpu_private_data.thread_index].data.owner;

		thread_number = allocate_thread();

		thread_table[thread_number].data.owner = process_number;
		thread_table[thread_number].data.registers.integer_registers.rflags = 0x200;
		thread_table[thread_number].data.registers.integer_registers.rip = prepare_process_ret_val.first_instruction_address;

		process_table[process_number].threads += 1;

		SYSCALL_ARGUMENTS.rax = ALL_OK;

		thread_queue_enqueue(&ready_queue,thread_number);
		/*cpu_private_data.thread_index = thread_number;*/


		break;
	}
	case SYSCALL_TERMINATE:
	{
		int i;
		int owner_process = thread_table[cpu_private_data.thread_index].data.owner;
		int parent_process = process_table[owner_process].parent;

		thread_table[cpu_private_data.thread_index].data.owner = -1; /* Terminate Thread */

		process_table[owner_process].threads -= 1; /* Decrement Thread count */

		if(process_table[owner_process].threads < 1) {
			cleanup_process(owner_process);
		}

		for(i=0; i < MAX_NUMBER_OF_THREADS && thread_table[i].data.owner != parent_process; i++) {
		}

		/*cpu_private_data.thread_index = i;*/
		/*thread_queue_dequeue(&ready_queue);*/
		schedule = 1;

		break;

	}


  /* Do not touch any lines above or including this line. */

  /* Add the implementation of more system calls here. */


  /* Do not touch any lines below or including this line. */
  default:
  {
   /* No system call defined. */
   SYSCALL_ARGUMENTS.rax = ERROR_ILLEGAL_SYSCALL;
  }
 }

 return schedule;
}
