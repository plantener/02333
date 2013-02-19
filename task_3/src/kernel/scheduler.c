/*!
 * \file scheduler.c
 * \brief
 *  This file holds the scheduler code. 
 */

#include "kernel.h"
int last_switch = 0;

void
scheduler_called_from_system_call_handler(const register int schedule)
{
	int thread_running, thread_to_run;

	if (schedule){
		/*This case, we reschedule*/
		last_switch = system_time;
		thread_to_run = thread_queue_dequeue(&ready_queue);
		cpu_private_data.thread_index = thread_to_run;
		return;

	}
	else {
		/*If current thread has used up its quantum (50 ms). */
		if (system_time - last_switch){
			last_switch = system_time;
			thread_running = cpu_private_data.thread_index;
			thread_queue_enqueue(&ready_queue,thread_running);
			thread_to_run = thread_queue_dequeue(&ready_queue);
			cpu_private_data.thread_index = thread_to_run;
			return;
		}
	}
}

void
scheduler_called_from_timer_interrupt_handler(const register int thread_changed)
{
}
