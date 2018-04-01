#
#include <pthread.h> 
#include <time.h> 
#include "errors.h"
#include <regex.h> 
#include <limits.h>

   //Structure of Alarm
   typedef struct alarm_tag {
      struct alarm_tag * link;
      //link in local, display_thread list
      struct alarm_tag * link_in_thread;
      int seconds;
      time_t time; /* seconds from EPOCH */
      int message_type;
      int message_number;
      //if alarm has been assigned to a display_thread
      long status;
      char message[128];
      //if alarm has been seen by alarm_thread
      int seen_by_alarm_thread;
      //Type A,B, or C
      char alarm_request_type;
   }
alarm_t;

//structure of thread
typedef struct display_thread_tag {
   struct display_thread_tag * link;
   pthread_t thread_id;
   int message_type;
}
display_thread_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t alarmA_req = PTHREAD_COND_INITIALIZER;
//alarm list
alarm_t * alarm_list = NULL;
//thread list
display_thread_t * thread_list = NULL;
int alarm_thread_created = 0;

//Insert Thread in global thread list
void thread_insert(display_thread_t * thread) {
   int status;
   display_thread_t * * last, * next;

   last = & thread_list;
   next = * last;
   while (next != NULL) {
      if (next->message_type >= thread->message_type) {
         thread->link = next; * last = thread;
         break;
      }
      last = & next->link;
      next = next->link;
   }

   if (next == NULL) { * last = thread;
      thread->link = NULL;
   }

#ifdef DEBUG
   printf("[thread: ");
   for (next = thread_list; next != NULL; next = next->link)
      printf("%d(%d)[\"%s\"] ", next->time,
         next->time /* = time (NULL)*/ , next->message);
   printf("]\n");
#endif

}

//if alarm list contains alarm with message number and request type return 1
int alarm_list_containsmn(int message_number, char alarm_request_type) {
   alarm_t * next;
   int status;

   for (next = alarm_list; next != NULL; next = next->link) {
      if (next->message_number == message_number && next->alarm_request_type == alarm_request_type) {
         return 1;
      }
   }
   return 0;

}

//Returns alarm with message number and request number
alarm_t * alarm_with_the_message_number(int message_number, char alarm_request_type) {
   alarm_t * next;
   int status;

   for (next = alarm_list; next != NULL; next = next->link) {
      if (next->message_number == message_number && alarm_request_type == next->alarm_request_type) {
         return next;
      }
   }
   return NULL;
}

//Returns alarm with message type and request type
alarm_t * alarm_with_the_message_type(int message_type, char alarm_request_type) {
      alarm_t * next;
      int status;

      for (next = alarm_list; next != NULL; next = next->link) {
         if (next->message_type == message_type && alarm_request_type == next->alarm_request_type) {
            return next;
         }
      }
      return NULL;
   }
   //Replace alarm with the new information
void alarm_replace(int message_number, int alarm_second, int message_type, char * message) {
   int status;

   alarm_t * alarm;
   alarm = alarm_with_the_message_number(message_number, 'A');
   alarm->seconds = alarm_second;
   alarm->time = time(NULL) + alarm->seconds;
   alarm->message_type = message_type;
   alarm->status = 0;
   alarm->message_number = message_number;
   alarm->seen_by_alarm_thread = 0;
   alarm->alarm_request_type = 'A';
   strcpy(alarm->message, message);

}

/**
 * check alarmlist contains alarm with message type and request type 
 * \param message_type message type,
 * \param alarm_request_type request type,
 * \return if 1
 */
int alarm_list_containsmt(int message_type, char alarm_request_type) {
   alarm_t * next;

   for (next = alarm_list; next != NULL; next = next->link) {
      if (next->message_type == message_type && 
          next->alarm_request_type == alarm_request_type) {
         return 1;
      }
   }
   return 0;

}

//Checks to see if thread list contains thread with same message type
int thread_list_containsmt(int message_type) {
   display_thread_t * next;
   int status;

   for (next = thread_list; next != NULL; next = next->link) {
      if (next->message_type == message_type) {
         return 1;
      }
      return 0;

   }
}

//insert alarm into global list
void alarm_insert(alarm_t * alarm) {
   int status;
   alarm_t **last, *next;
   /*
    *Call for mutex so the conditon variable in thread to synched with this function
    */
   status = pthread_mutex_lock( &alarm_mutex);
   if (status != 0)
      err_abort(status, "Lock mutex");
   /*
    *Place alarm in list by message type
    */
   last = & alarm_list;
   next = * last;
   while (next != NULL) {
      if (next->message_type >= alarm->message_type) {
         alarm->link = next; * last = alarm;
         break;
      }
      last = & next->link;
      next = next->link;
   }
   if (next == NULL) { * last = alarm;
      alarm->link = NULL;
   }

#ifdef DEBUG
   printf("[list: ");
   for (next = alarm_list; next != NULL; next = next->link)
      printf("%d(%d)[\"%s\"] ", next->time,
         next->time /* = time (NULL)*/ , next->message);
   printf("]\n");
#endif

   status = pthread_mutex_unlock( & alarm_mutex);
   if (status != 0)
      err_abort(status, "Unlock mutex");
   status = pthread_cond_signal( & alarm_cond);
   if (status != 0)
      err_abort(status, "Signal cond");

}

//remove alarm from global list, and free it
void alarm_remover(alarm_t * alarm) {
   alarm_t * temp_alarm, * temp_alarm_past;
   int status;
   temp_alarm_past = NULL;
   /*
    *Lock mutex so the threads are synched
    */
   status = pthread_mutex_lock( & alarm_mutex);
   if (status != 0)
      err_abort(status, "Lock mutex");

   for (temp_alarm = alarm_list; temp_alarm != NULL; temp_alarm_past = temp_alarm, temp_alarm = (temp_alarm->link)) {
      if (temp_alarm == alarm) {
         if (temp_alarm_past == NULL) {
            alarm_list = temp_alarm->link;
         } else {
            temp_alarm_past->link = temp_alarm->link;
         }
         temp_alarm->link = NULL;
         free(temp_alarm);
         break;
      }
   }
#ifdef DEBUG
   printf("[list: ");
   for (temp_alarm = alarm_list; temp_alarm != NULL; temp_alarm = temp_alarm->link)
      printf("%d(%d)[\"%s\"] ", temp_alarm->time,
         temp_alarm->time /* = time (NULL)*/ , temp_alarm->message);
   printf("]\n");
#endif

   status = pthread_mutex_unlock( & alarm_mutex);
   if (status != 0)
      err_abort(status, "Unlock mutex");

}

//deletes threads that are no longer needed
void obsolescent_thread(int terminated_message_type) {
   display_thread_t * temp_thread, * temp_thread_past;
   /*
    *Remove thread of MessageType(x) from list, and cancel thread
    */
   temp_thread_past = NULL;
   for (temp_thread = thread_list; temp_thread != NULL;) {
      if ((temp_thread->message_type) == terminated_message_type) {
         /*
          *Terminate thread and remove from linked list
          */
         pthread_cancel(temp_thread->thread_id);
         if (thread_list == temp_thread)
            thread_list = temp_thread->link;
         else
            temp_thread_past->link = temp_thread->link;
         free(temp_thread);
         if (temp_thread_past == NULL) {
            temp_thread = thread_list;

         } else {
            temp_thread = temp_thread_past->link;
         }

         break;
      } else {
         temp_thread_past = temp_thread;
         temp_thread = (temp_thread->link);

      }
   }
}

//cleanup after terminated display_thread
void thread_terminate_cleanup(void * list) {
   alarm_t * * last, * next, * temp;
   int status;
   last = (alarm_t * * )( & list);
   next = * last;
   while (next != NULL) {
      temp = next->link;
      free(next);
      next = temp;
   }

#ifdef DEBUG
   printf("[list: ");
   temp_alarm = (alarm_t * ) list;
   for (; temp_alarm != NULL; temp_alarm = temp_alarm->link)
      printf("%d(%d)[\"%s\"] ", temp_alarm->time,
         temp_alarm->time /* = time (NULL)*/ , temp_alarm->message);
   printf("]\n");
#endif

   status = pthread_mutex_unlock( & alarm_mutex);
   if (status != 0)
      err_abort(status, "Unlock mutex");
}

//periodic_display_threads
void * periodic_display_threads(void * arg) {
   //holds the current alarm being kept, and the local list
   alarm_t * alarm, * current_alarm, * thread_alarm_list;
   time_t now;
   int status;
   int type_of_thread = * ((int * ) arg);
   free(arg);

   current_alarm = NULL;
   thread_alarm_list = NULL;

   pthread_cleanup_push(thread_terminate_cleanup, (void * ) thread_alarm_list);

   while (1) {

      status = pthread_yield();
      if (status != 0)
         err_abort(status, "Thread Yield");

      status = pthread_mutex_lock( & alarm_mutex);
      if (status != 0)
         err_abort(status, "Lock mutex");

      alarm = alarm_list;

      if (alarm != NULL) {
         while (alarm->message_type != type_of_thread || alarm->status != 0 || alarm->alarm_request_type != 'A') {
            if (alarm->link != NULL)
               alarm = alarm->link;
            else {
               alarm = NULL;
               break;
            }

         }
      }

      if (alarm == NULL && thread_alarm_list == NULL) {
         status = pthread_cond_wait( & alarmA_req, & alarm_mutex);
         if (status != 0)
            err_abort(status, "Wait on cond");
         sleep(0);
         status = pthread_mutex_unlock( & alarm_mutex);
         if (status != 0)
            err_abort(status, "Unlock mutex");
         continue;
      } else {
         sleep(0);
         status = pthread_mutex_unlock( & alarm_mutex);
         if (status != 0)
            err_abort(status, "Unlock mutex");
         if (alarm != NULL) {
            alarm->status = pthread_self();
            alarm->time = time(NULL) + alarm->seconds;
            alarm_t * * last, * next;

            last = & thread_alarm_list;
            next = * last;
            while (next != NULL) {
               if (next->time >= alarm->time) {
                  alarm->link_in_thread = next; * last = alarm;
                  break;
               }
               last = & next->link_in_thread;
               next = next->link_in_thread;
            }

            if (next == NULL) { * last = alarm;
               alarm->link_in_thread = NULL;
            }

            current_alarm = thread_alarm_list;
            alarm = NULL;
         }

         if (current_alarm->message_type != type_of_thread) {
            printf("Stopped Displaying Replaced Alarm With Message Type (%d) at %d:A\n", current_alarm->message_type, time(NULL));
            if (current_alarm->link_in_thread == NULL)
               thread_alarm_list = NULL;
            else
               current_alarm->status = 0;
            current_alarm->link_in_thread = NULL;
            thread_alarm_list = current_alarm->link_in_thread;
            current_alarm = thread_alarm_list;
            continue;
         }

         now = time(NULL);
         if (current_alarm->time <= now) {
            status = pthread_mutex_lock( & print_mutex);
            if (status != 0)
               err_abort(status, "Lock print mutex");

            printf("(%d) %s\n", current_alarm->seconds, current_alarm->message);
            printf("Alarm With Message Type (%d) and Message Number (%d) Displayed at %d: A \n", current_alarm->message_type, current_alarm->message_number, time(NULL));

            status = pthread_mutex_unlock( & print_mutex);
            if (status != 0)
               err_abort(status, "Unlock print mutex");

            if (current_alarm->link_in_thread == NULL)
               thread_alarm_list = NULL;
            else
               thread_alarm_list = current_alarm->link_in_thread;
            alarm_remover(current_alarm);
            current_alarm = thread_alarm_list;

         } else
            continue;

      }

   }

   pthread_cleanup_pop(1);
}

//alarm thread
void * alarm_thread(void * arg) {
   //gets status of lock
   int status;
   alarm_t * alarm;
   time_t now;
   pthread_t thread;
   display_thread_t * thread_node;
   thread_node = NULL;

   status = pthread_mutex_lock( &alarm_mutex);
   if (status != 0)
      err_abort(status, "Lock mutex");

   while (1) {
      status = pthread_cond_wait( &alarm_cond, &alarm_mutex);
      if (status != 0)
         err_abort(status, "Wait on cond");

      status = pthread_mutex_lock( &print_mutex);
      if (status != 0)
         err_abort(status, "Lock print mutex");

      alarm_t * next;
      //get alarm from alarm_list that hasn't been seen by alarm_thread
      for (next = alarm_list; next != NULL; next = next->link) {
         if (next->seen_by_alarm_thread == 0)
            break;
      }
      //If Request A
      if (next->alarm_request_type == 'A') {
         status = pthread_cond_broadcast( & alarmA_req);
         if (status != 0)
            err_abort(status, "Broadcast cond");
         next->seen_by_alarm_thread = 1;
         display_thread_t * next_thread;
         for (next_thread = thread_list; next_thread != NULL; next_thread = next_thread->link) {
            if (!alarm_list_containsmt(next_thread->message_type, 'A')) {
               printf("Type A Alarm Request Processed at %d: Periodic Display Thread For Message Type (%d) Terminated: No more Alarm Requests For Message Type (%d). \n", time(NULL), next_thread->message_type, next_thread->message_type);
               status = pthread_mutex_unlock( & alarm_mutex);
               if (status != 0)
                  err_abort(status, "unlock mutex");

               alarm_remover(alarm_with_the_message_type(next_thread->message_type, 'B'));
               obsolescent_thread(next_thread->message_type);

               status = pthread_mutex_lock( & alarm_mutex);
               if (status != 0)
                  err_abort(status, "Lock mutex");

            }
         }

      }
      //If request B
      if (next->alarm_request_type == 'B') {
         int * i = malloc(sizeof( * i)); * i = next->message_type;
         status = pthread_create( & thread, NULL, periodic_display_threads, (void * ) i);
         if (status != 0)
            err_abort(status, "Create alarm thread");
         //place thread in thread list
         thread_node = (display_thread_t * ) calloc(1, sizeof(display_thread_t));
         thread_node->thread_id = thread;
         thread_node->message_type = next->message_type;
         thread_insert(thread_node);
         next->seen_by_alarm_thread = 1;
         printf("Type B Alarm Request Processed at %d: New Periodic Display Thread For Message Type (%d) Created. \n", time(NULL), next->message_type);
      }
      //If request C
      if (next->alarm_request_type == 'C') {
         int terminate_message_number = next->message_number;

         status = pthread_mutex_unlock( & alarm_mutex);
         if (status != 0)
            err_abort(status, "unlock mutex");

         alarm_t * terminated_alarm = alarm_with_the_message_number(terminate_message_number, 'A');
         int terminate_message_type = terminated_alarm->message_type;
         alarm_remover(terminated_alarm);

         status = pthread_mutex_lock( & alarm_mutex);
         if (status != 0)
            err_abort(status, "Lock mutex");

         next->seen_by_alarm_thread = 1;
         printf("Type C Alarm Request Processed at %d: Alarm Request With Message Number (%d) Removed\n", time(NULL), next->message_number);

         display_thread_t * next_thread;
         for (next_thread = thread_list; next_thread != NULL; next_thread = next_thread->link) {
            if (!alarm_list_containsmt(terminate_message_type, 'A')) {
               printf("Type C Alarm Request Processed at %d: Periodic Display Thread For Message Type (%d) Terminated: No more Alarm Requests For Message Type (%d). \n", time(NULL), terminate_message_type, terminate_message_type);
               status = pthread_mutex_unlock( & alarm_mutex);
               if (status != 0)
                  err_abort(status, "unlock mutex");

               alarm_remover(alarm_with_the_message_type(terminate_message_type, 'B'));
               obsolescent_thread(terminate_message_type);

               status = pthread_mutex_lock( & alarm_mutex);
               if (status != 0)
                  err_abort(status, "Lock mutex");
            }
         }
         status = pthread_mutex_unlock( & alarm_mutex);
         if (status != 0)
            err_abort(status, "unlock mutex");

         alarm_remover(next);

         status = pthread_mutex_lock( & alarm_mutex);
         if (status != 0)
            err_abort(status, "Lock mutex");
      }

      status = pthread_mutex_unlock( & print_mutex);
      if (status != 0)
         err_abort(status, "Unlock print mutex");

   }
}

/**
 * classify command type
 * \param line information that user input.
 * \param msg_type Output message type.
 * \param alarm_second If the command is message command, after the alarm_second, 
 *                    message will be displayed.
 * \param message If the command is message command. message contains the message to be
 *               displayed.
 * \return 1 means create type B alarm request,
 *         2 means create type C alarm request, 
 *         3 means create type A alarm request, 
 *         -1 means bad command.
 */
int get_cmd_type(char * line, int * msg_type, unsigned int * alarm_second, int * message_number, char * message) {
   char cmd[60];
   char str_msg_type[60];
   char str_msg_2[30];
   int ret_value;

   if (sscanf(line, "%d %s %s %128[^\n]", alarm_second, str_msg_type, str_msg_2, message) == 4) {
      sscanf(str_msg_type, "%*[^-0123456789]%d", msg_type);
      sscanf(str_msg_2, "%d", message_number);
      if ( * msg_type < 1 || * message_number < 1) {
         fprintf(stderr, "Message type and message number must be the positive integer\n");
         ret_value = -1;
      } else {
         ret_value = 3;
      }

   } else if (sscanf(line, "%s %s[^\n]", cmd, str_msg_type) == 2) {
      if (sscanf(str_msg_type, "%*[^-0123456789]%d", msg_type) == 1 &&
         strncmp(str_msg_type, "MessageType(", strlen("MessageType(") - 1) == 0) {
         if ( * msg_type < 1) {
            fprintf(stderr, "Message type must be the positive integer.\n");
            ret_value = -1;
         } else if (strcmp(cmd, "Create_Thread:") == 0) {
            ret_value = 1;
         } else {
            ret_value = -1;
         }
      } else if (sscanf(str_msg_type, "%*[^-0123456789]%d", message_number) == 1 &&
         strncmp(str_msg_type, "Message(", strlen("Message(") - 1) == 0) {
         if ( * message_number < 1) {
            fprintf(stderr, "Message number must be the positive integer.\n");
            ret_value = -1;
         } else if (strcmp(cmd, "Cancel:") == 0) {
            ret_value = 2;
         } else {
            ret_value = -1;
         }
      } else {
         ret_value = -1;
      }
   } else {
      fprintf(stderr, "The number of parameters is not correct.\n");
      ret_value = -1;
   }

   return ret_value;
}

//Main Function, or Main thread
int main(int argc, char * argv[]) {
   //Gets error if using pthread funciton
   int status;
   //gets the command line
   char line[256];
   //gets the message
   char message[128];
   //gets the message number
   int message_number;
   //gets message type
   int message_type;
   //gets time of message
   unsigned int alarm_second;
   alarm_t * alarm;
   int cmd_type;
   pthread_t thread;

   //if alarm thread hasn't been created yet, make 1
   if (alarm_thread_created == 0) {
      alarm_thread_created = 1;
      status = pthread_create( & thread, NULL, alarm_thread, NULL);
      if (status != 0)
         err_abort(status, "Create alarm thread");
   }

   //Loop runs until terminated
   while (1) {
      status = pthread_mutex_lock( & print_mutex);
      if (status != 0)
         err_abort(status, "Lock print mutex");
      printf("Alarm> ");

      display_thread_t * nextzs;
      printf("[thread: ");
      for (nextzs = thread_list; nextzs != NULL; nextzs = nextzs->link)
         printf("%d :", nextzs->message_type);
      printf("]\n");

      alarm_t * temp_alarmzs;
      printf("[list: ");
      for (temp_alarmzs = alarm_list; temp_alarmzs != NULL; temp_alarmzs = temp_alarmzs->link)
         printf("%d(%d)[\"%s\"] ", temp_alarmzs->time, temp_alarmzs->message_type, temp_alarmzs->message);
      printf("]\n");

      status = pthread_mutex_unlock( & print_mutex);
      if (status != 0)
         err_abort(status, "Unlock print mutex");
      if (fgets(line, sizeof(line), stdin) == NULL) exit(0);
      if (strlen(line) <= 1) continue;

      //Get Command Type
      cmd_type = get_cmd_type(line, & message_type, & alarm_second, & message_number, message);
      switch (cmd_type) {
         //If Type B request
      case 1:
         {
            status = pthread_mutex_lock( & print_mutex);
            if (status != 0)
               err_abort(status, "Lock print mutex");
            //Does not exist any Type A alarm Request with same Message Type
            if (!alarm_list_containsmt(message_type, 'A')) {
               printf("Type B Alarm Request Error: No Alarm Request With Message Type (%d)!\n", message_type);
               status = pthread_mutex_unlock( & print_mutex);
               if (status != 0)
                  err_abort(status, "Unlock print mutex");
               break;
            }
            //IF typeB already exists
            if (alarm_list_containsmt(message_type, 'B')) {
               printf("Error: More Than One Type B Alarm Request With Message Type (%d)!\n", message_type);
               status = pthread_mutex_unlock( & print_mutex);
               if (status != 0)
                  err_abort(status, "Unlock print mutex");
               break;
            }
            //else place in alarm list

            alarm = (alarm_t * ) malloc(sizeof(alarm_t));
            if (alarm == NULL)
               errno_abort("Allocate alarm");
            //alarm->seconds = alarm_second;
            //alarm->time = time (NULL) + alarm->seconds;
            alarm->message_type = message_type;
            alarm->status = 0;
            alarm->link = NULL;
            //alarm->message_number=message_number;
            alarm->seen_by_alarm_thread = 0;
            alarm->alarm_request_type = 'B';
            //strcpy(alarm->message, message);
            alarm_insert(alarm);

            printf("Type B Create Thread Alarm Request For Message Type (%d) Inserted Into Alarm List at %d!\n",
                   message_type, time(NULL));
            status = pthread_mutex_unlock( & print_mutex);
            if (status != 0)
               err_abort(status, "Unlock print mutex");
            break;

            // Type C
         }
      case 2:
         {
            status = pthread_mutex_lock( & print_mutex);
            if (status != 0)
               err_abort(status, "Lock print mutex");

            //No Type A with same message_number
            if (!alarm_list_containsmn(message_number, 'A')) {
               printf("Error: No Alarm Request With Message Number (%d) to Cancel!\n", message_number);
               status = pthread_mutex_unlock( & print_mutex);
               if (status != 0)
                  err_abort(status, "Unlock print mutex");
               break;
            }

            if (alarm_list_containsmn(message_number, 'C')) {
               //Already a cancel request made
               printf("Error: More Than One Request to Cancel Alarm Request With Message Number (%d)!\n", message_number);
               status = pthread_mutex_unlock( & print_mutex);
               if (status != 0)
                  err_abort(status, "Unlock print mutex");
               break;
            }

            alarm = (alarm_t * ) malloc(sizeof(alarm_t));
            if (alarm == NULL)
               errno_abort("Allocate alarm");
            //alarm->seconds = alarm_second;
            //alarm->time = time (NULL) + alarm->seconds;
            //alarm->message_type = message_type;
            alarm->status = 0;
            alarm->link = NULL;
            alarm->message_number = message_number;
            alarm->seen_by_alarm_thread = 0;
            alarm->alarm_request_type = 'C';
            //strcpy(alarm->message, message);
            alarm_insert(alarm);

            printf("Type C Cancel Alarm Request With Message Number (%d) Inserted Into Alarm List at %d: C\n", message_number, time(NULL));

            status = pthread_mutex_unlock( & print_mutex);
            if (status != 0)
               err_abort(status, "Unlock print mutex");
            break;

            //Type A
         }
      case 3:
         {
            status = pthread_mutex_lock( & print_mutex);
            if (status != 0)
               err_abort(status, "Lock print mutex");
            //if type A with same message_number exists replace it
            if (alarm_list_containsmn(message_number, 'A')) {
               alarm_replace(message_number, alarm_second, message_type, message);
               printf("Type A Replacement Alarm Request With Message Number (%d) Inserted Into Alarm List at %d: A\n", message_number, time(NULL));

            } else {

               alarm = (alarm_t * ) malloc(sizeof(alarm_t));
               if (alarm == NULL)
                  errno_abort("Allocate alarm");
               alarm->seconds = alarm_second;
               alarm->time = time(NULL) + alarm->seconds;
               alarm->message_type = message_type;
               alarm->status = 0;
               alarm->link = NULL;
               alarm->seen_by_alarm_thread = 0;
               alarm->alarm_request_type = 'A';
               alarm->message_number = message_number;
               strcpy(alarm->message, message);
               alarm_insert(alarm);
               printf("Type A Alarm Request With Message Number (%d) Inserted Into Alarm List at %d: A\n", alarm->message_number, time(NULL));
            }
            status = pthread_mutex_unlock( & print_mutex);
            if (status != 0)
               err_abort(status, "Unlock print mutex");
            break;

         }
      case -1:
         {
            fprintf(stderr, "Bad command\n");
            break;
         }
      }
   }

   status = pthread_yield();
   if (status != 0)
      err_abort(status, "Thread Yield");
}
