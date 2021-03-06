#include <pthread.h> 
#include <semaphore.h>
#include <time.h> 
#include "errors.h"
#include <regex.h> 
#include <limits.h>

#define READ_SEMAPHORE_START sem_wait(&semaphore_mutex);\
   readcount++;\
   if(readcount == 1){\
       sem_wait(&wrt);\
   }\
   sem_post(&semaphore_mutex);\
   
#define READ_SEMAPHORE_END sem_wait(&semaphore_mutex);\
         readcount--;\
         if(readcount == 0){\
             sem_post(&wrt);\
         }\
         sem_post(&semaphore_mutex);\
         
//Structure of Alarm
typedef struct alarm_tag {
  struct alarm_tag * link;
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

sem_t semaphore_mutex, wrt;
int readcount = 0;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t alarmA_req = PTHREAD_COND_INITIALIZER;
//alarm list which is sorted by message number
alarm_t * alarm_list = NULL;
//thread list
display_thread_t * thread_list = NULL;
int alarm_thread_created = 0;

/**
 * Insert thread in global thread list. Use thread list to manage the periodic 
 * display thread.
 * \param thread Thread node to be inserted into thread list.
 */
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
      printf("%d(%d) ", next->message_type,
         next->thread_id /* = time (NULL)*/);
   printf("]\n");
#endif

}

//if alarm list contains alarm with message number and request type return 1
int alarm_list_containsmn(int message_number, char alarm_request_type) {
   alarm_t * next;
   int status;

   READ_SEMAPHORE_START
   
   for (next = alarm_list; next != NULL; next = next->link) {
      if (next->message_number == message_number && next->alarm_request_type == alarm_request_type) {
         READ_SEMAPHORE_END
         return 1;
      }
   }
   
   READ_SEMAPHORE_END
   
   return 0;

}

/**
 * Find alarm with the given message number and request type.
 * \param message_number Message number are going to be found in global alarm list.
 * \param alarm_request_type Alarm request type are going to be found in global alarm list.
 * \return Alarm point if that alarm is found in the global alarm list, otherwise return NULL.
 */
alarm_t * alarm_with_the_message_number(int message_number, char alarm_request_type) {
   alarm_t * next;
   int status;

   READ_SEMAPHORE_START
   
   for (next = alarm_list; next != NULL; next = next->link) {
      if (next->message_number == message_number && alarm_request_type == next->alarm_request_type) {
         READ_SEMAPHORE_END
         
         return next;
      }
   }
   READ_SEMAPHORE_END
   
   return NULL;
}

/**
 * Find alarm with the given message type and request type.
 * \param message_type Message type are going to be found in global alarm list.
 * \param alarm_request_type Alarm request type are going to be found in global alarm list.
 * \return Alarm point if that alarm is found in the global alarm list, otherwise return NULL.
 */
alarm_t * alarm_with_the_message_type(int message_type, char alarm_request_type) {
   alarm_t * next;
   int status;

   READ_SEMAPHORE_START
   
   for (next = alarm_list; next != NULL; next = next->link) {
      if (next->message_type == message_type && alarm_request_type == next->alarm_request_type) {
         READ_SEMAPHORE_END
         
         return next;
      }
   }
   READ_SEMAPHORE_END
         
   return NULL;
}

/**
 * Replace alarm with the new information. If that message number is not found int alarm 
 * list, it will do nothing.
 * \param message_number The infomation of the alarm with this message_number will be changed.
 * \param alarm_second New alarm second will be replaced to that alarm.
 * \param message_type New message type will be replaced to that alarm.
 * \param message New message will be replaced to that alarm.
 */
void alarm_replace(int message_number, int alarm_second, int message_type, char * message) {
   int status;

   alarm_t * alarm;
   alarm = alarm_with_the_message_number(message_number, 'A');
   if(alarm == NULL){
      return;
   }

   alarm->seconds = alarm_second;
   alarm->time = time(NULL) + alarm->seconds;
   alarm->message_type = message_type;
   alarm->status = 0;
   alarm->message_number = message_number;
   alarm->seen_by_alarm_thread = 0;
   alarm->alarm_request_type = 'A';
   strcpy(alarm->message, message);
#ifdef DEBUG
   printf("message number %d has been replaced\n", message_number);
   alarm_t* next;
   printf("[list replaced: ");
   for (next = alarm_list; next != NULL; next = next->link)
      printf("%d(%d)[\"%s\"] ", next->message_type,
         next->message_number /* = time (NULL)*/ , next->message);
   printf("]\n");
   printf("alarm signal\n");
#endif
   status = pthread_cond_signal( & alarm_cond);
   //status = pthread_cond_broadcast( & alarm_cond);
   if (status != 0)
      err_abort(status, "Signal cond");
}

/**
 * check alarmlist contains alarm with message type and request type 
 * \param message_type message type,
 * \param alarm_request_type request type,
 * \return if 1
 */
int alarm_list_containsmt(int message_type, char alarm_request_type) {
   alarm_t * next;

   READ_SEMAPHORE_START
   
   for (next = alarm_list; next != NULL; next = next->link) {
      if (next->message_type == message_type && 
          next->alarm_request_type == alarm_request_type) {
         READ_SEMAPHORE_END
         
         return 1;
      }
   }
   READ_SEMAPHORE_END
         
   return 0;

}

/**
 * Checks to see if thread list contains thread with same message type.
 * \param message_type Check parameter message type is already in the thread list
 * or not. If it existed, it means that the periodic display thread has been created.
 * \return 1 If it is existed, otherwise return 0.
 */
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

/**
 * Insert alarm into global alarm list.
 * \param alarm Alarm to be inserted into global alarm list.
 */
void alarm_insert(alarm_t * alarm) {
   int status;
   alarm_t **last, *next;
   /*
    *Place alarm in list by message type
    */
   last = & alarm_list;
   next = * last;
   sem_wait(&wrt);
   while (next != NULL) {
      if (next->message_number > alarm->message_number) {
         alarm->link = next;
         * last = alarm;
         break;
      }
      last = & next->link;
      next = next->link;
   }
   if (next == NULL) {
      * last = alarm;
      alarm->link = NULL;
   }

#ifdef DEBUG
   printf("[list: ");
   for (next = alarm_list; next != NULL; next = next->link)
      printf("%d(%d)[\"%s\"] ", next->message_type,
         next->message_number /* = time (NULL)*/ , next->message);
   printf("]\n");
#endif
   sem_post(&wrt);
   status = pthread_cond_signal( & alarm_cond);
   //status = pthread_cond_broadcast( & alarm_cond);
   if (status != 0)
      err_abort(status, "Signal cond");
}

/**
 * Insert alarm into local alarm list which is in the periodic thread.
 * \param alarm Alarm to be inserted.
 * \param local_alarm_list Local alarm list which is in the periodic thread.
 */

void local_alarm_insert(alarm_t * alarm, alarm_t** local_alarm_list) {
   int status;
   alarm_t *current;
   /*
    *Place alarm in list by message type
    */
   sem_wait(&wrt);
   if(*local_alarm_list == NULL || (*local_alarm_list)->time >= alarm->time){
      alarm->link = *local_alarm_list;
      *local_alarm_list = alarm;
   }else{
      current = *local_alarm_list;
      while (current->link != NULL && current->time < alarm->time) {
         current = current->link;
      }
      alarm->link = current->link;
      current->link = alarm;
   }

#ifdef DEBUG
   printf("[local list: ");
   for (current = *local_alarm_list; current != NULL; current = current->link)
      printf("%d(%d)[\"%s\"] time2: %d", current->message_type,
         current->message_number /* = time (NULL)*/ , current->message, current->time);
   printf("]\n");
#endif
   sem_post(&wrt);
   return;
}

/**
 * Remove alarm from global list and free it.
 * \param alarm Alarm is to be removed from alarm list.
 */
void alarm_remover(alarm_t * alarm) {
   alarm_t * temp_alarm, * temp_alarm_past;
   int status;
   temp_alarm_past = NULL;
   /*
    *Lock mutex so the threads are synched
    */
   sem_wait(&wrt);

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
      printf("%d(%d)[\"%s\"] ", temp_alarm->message_type,
         temp_alarm->message_number /* = time (NULL)*/ , temp_alarm->message);
   printf("]\n");
#endif
   sem_post(&wrt);
}

/**
 * Check if the message_type has been changed.
 * \param alarm this alarm is in the periodically display thread.
 * \return 1 If that alarm message type has been changed in the global alarm list,
 * otherwise return 0.
 */
int message_type_changed(alarm_t* alarm)
{
   alarm_t *tmp_alarm;
   READ_SEMAPHORE_START
   
   for(tmp_alarm = alarm_list; tmp_alarm != NULL; tmp_alarm = tmp_alarm->link){
      if(alarm->message_number == tmp_alarm->message_number &&
         alarm->message_type != tmp_alarm->message_type){
            READ_SEMAPHORE_END
            return 1;
      }
   }
   READ_SEMAPHORE_END
   return 0;
}

/**
 * Check if the new alarm is already in the local alarm list. Refer to test case 18.
 * This case is when user changes the message information for the existed message number 
 * alarm.
 * \param alarm New alarm found in the alarm_list.
 * \param thread_alarm_list Local alarm list of periodic display thread.
 * \return If new alarm is already in the local alarm list, return the point of that 
 *         alarm. Otherwise return NULL.
 */
alarm_t* get_existed_alarm(alarm_t* alarm, alarm_t* thread_alarm_list)
{
   alarm_t *tmp_alarm;
   for(tmp_alarm = thread_alarm_list; tmp_alarm != NULL; tmp_alarm = tmp_alarm->link){
      if(alarm->message_number == tmp_alarm->message_number){
         return tmp_alarm;
      }
   }

   return NULL;
}

/**
 * Check if the alarm is removed in alarm_list.
 * \param alarm This alarm is in the periodically display thread.
 * \return 1 If that alarm message type has been removed in the global alarm list,
 * otherwise return 0.
 */
int alarm_removed(alarm_t *alarm)
{
   alarm_t *tmp_alarm;
   READ_SEMAPHORE_START
   
   for(tmp_alarm = alarm_list; tmp_alarm != NULL; tmp_alarm = tmp_alarm->link){
      if(alarm->message_number == tmp_alarm->message_number &&
         alarm->message_type == tmp_alarm->message_type){
            READ_SEMAPHORE_END
            return 0;
      }
   }
   READ_SEMAPHORE_END
   return 1;
}

/**
 * Periodic display threads, This thread will keep displaying the type A alarm in its
 * local alarm list. 
 * It will periodically check if there is new the message type that it is this thread
 * responsible for. If it has found, it will copy that new alarm into its local
 * alarm list. 
 * It also periodically checks if there is an alarm removed from global alarm list or
 * alarm message type has been change. If it has found, it will remove that alarm from
 * local alarm list.
 * If there is no alarm in the local alarm list, this thread will be terminated by itself
 * \param arg message type
 */
void * periodic_display_threads(void * arg) {
   //holds the current alarm being kept, and the local list
   alarm_t *alarm, *current_alarm, *thread_alarm_list;  //thread_alarm_list sorted by time
   alarm_t *last_alarm;
   time_t now;
   int status;
   int type_of_thread = * ((int * ) arg);
   struct timespec cond_time;

   current_alarm = NULL;
   thread_alarm_list = NULL;

   while (1) {
      /* check if there is alarm which the message type has been changed */
      alarm_t **p_alarm = &thread_alarm_list;
      while(*p_alarm && !message_type_changed(*p_alarm) && !alarm_removed(*p_alarm)){
         p_alarm = &(*p_alarm)->link;
      }

      if(*p_alarm){
         alarm_t *alarm_del = *p_alarm;
         *p_alarm = alarm_del->link;
         printf("Stopped Displaying Replaced Alarm With Message Type (%d) at %d:A\n", alarm_del->message_type, time(NULL));
         free(alarm_del);
      }

      READ_SEMAPHORE_START
      alarm = alarm_list;

      /* get an alarm which message_type == the given parameter and status == 0 and 
         alarm_request_type == 'A' */
      while (alarm != NULL && (alarm->message_type != type_of_thread ||
             alarm->status != 0 || alarm->alarm_request_type != 'A')) {
         alarm = alarm->link;
      }
      READ_SEMAPHORE_END
      if(alarm != NULL){
         alarm_t* existed_alarm = get_existed_alarm(alarm, thread_alarm_list);
         if(existed_alarm != NULL){
            strcpy(existed_alarm->message, alarm->message); 
         }else{
            alarm->status = pthread_self();
            current_alarm = (alarm_t*)malloc(sizeof(alarm_t));
            memcpy(current_alarm, alarm, sizeof(alarm_t));
            current_alarm->link = NULL;
            local_alarm_insert(current_alarm, &thread_alarm_list); 
         }
      }
      if (thread_alarm_list == NULL) {
#ifdef DEBUG          
         printf("thread %d exit\n", pthread_self());
#endif
         status = pthread_mutex_unlock( & alarm_mutex);
         if (status != 0)
            err_abort(status, "unlock mutex");
         return;
      } else {
         /* remove the first alarm and assign to current_alarm */
#ifdef DEBUG
   printf("[thread_alarm_list: ");
   alarm_t *tmp;
   for (tmp = thread_alarm_list; tmp != NULL; tmp = tmp->link)
      printf("%d(%d)[\"%s\"] time1:%d", tmp->message_type,
         tmp->message_number /* = time (NULL)*/ , tmp->message, tmp->time);
   printf("]\n");
#endif
         current_alarm = thread_alarm_list;
         thread_alarm_list = thread_alarm_list->link;
         current_alarm->link = NULL;
#ifdef DEBUG
   printf("[thread_alarm_list: ");

   for (tmp = thread_alarm_list; tmp != NULL; tmp = tmp->link)
      printf("%d(%d)[\"%s\"] ", tmp->message_type,
         tmp->message_number /* = time (NULL)*/ , tmp->message, tmp->time);
   printf("]\n");
#endif

         now = time(NULL);
         int expired = 0;
         if(current_alarm->time > now){
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", current_alarm->message_number,
                    current_alarm->time - time (NULL), current_alarm->message);
#endif
            cond_time.tv_sec = current_alarm->time;
            cond_time.tv_nsec = 0;
            time_t current_time = current_alarm->time;
            while (current_time == current_alarm->time) {
                status = pthread_cond_timedwait (
                    &alarmA_req, &alarm_mutex, &cond_time);
                if (status == ETIMEDOUT) {
                    expired = 1;
                    break;
                }
                if (status != 0)
                    err_abort (status, "Cond timedwait");
            }
            if (!expired)
                local_alarm_insert (current_alarm, &thread_alarm_list);
         }else{
            expired = 1;
         }
         if (expired) {
#ifdef DEBUG
            printf("(%d) %s\n", current_alarm->seconds, current_alarm->message);
#endif
            printf("Alarm With Message Type (%d) and Message Number (%d) Displayed at %d: A \n",
                   current_alarm->message_type, current_alarm->message_number, time(NULL));
            current_alarm->time = time(NULL) + current_alarm->seconds;
            local_alarm_insert(current_alarm, &thread_alarm_list);
         }
      }
   } //end of while
}

/**
 * Alarm thread to check the new alarm in the alarm list. 
 * After finding message type of new alarm of type A was changed and there is no original
 * message type existed in alarm list, then remove the related type B alarm and terminate 
 * the related periodic display thread
 * If find a new message type of B, it will create a new periodic display thread for that
 * message type
 * If find a new message type of C, it will check the message number of alarm meesage type 
 * A is existed in alarm list, if it is existed, that alarm will be removed from alarm list.
 * If that is the only alarm of that message type, the periodic display thread will be 
 * terminated.
 */
void * alarm_thread(void * arg) {
   //gets status of lock
   int status;
   alarm_t * alarm;
   time_t now;
   pthread_t thread;
   display_thread_t * thread_node;
   thread_node = NULL;

   while (1) {
      status = pthread_cond_wait( &alarm_cond, &alarm_mutex);
      if (status != 0)
         err_abort(status, "Wait on cond");
      alarm_t * next;
      READ_SEMAPHORE_START
      //get alarm from alarm_list that hasn't been seen by alarm_thread
      for (next = alarm_list; next != NULL; next = next->link) {
         if (next->seen_by_alarm_thread == 0)
            break;
      }
      READ_SEMAPHORE_END
      //If Request A
      if (next->alarm_request_type == 'A') {
         next->seen_by_alarm_thread = 1;
         display_thread_t * next_thread;
         for (next_thread = thread_list; next_thread != NULL; next_thread = next_thread->link) {
            if (!alarm_list_containsmt(next_thread->message_type, 'A')) {
               alarm_remover(alarm_with_the_message_type(next_thread->message_type, 'B'));
               
               printf("Type A Alarm Request Processed at %d: Periodic Display Thread For Message Type (%d) Terminated: No more Alarm Requests For Message Type (%d). \n", time(NULL), next_thread->message_type, next_thread->message_type);
            }
         }
         status = pthread_cond_broadcast(&alarmA_req);
         if(status != 0)
            err_abort(status, "Broadcast cond");
      }
      //If request B
      if (next->alarm_request_type == 'B') {
         int * i = malloc(sizeof( * i));
         * i = next->message_type;
         status = pthread_create( & thread, NULL, periodic_display_threads, (void * ) i);
         if (status != 0)
            err_abort(status, "Create periodic display thread");
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

         alarm_t * terminated_alarm = alarm_with_the_message_number(terminate_message_number, 'A');
         int terminate_message_type = terminated_alarm->message_type;
         alarm_remover(terminated_alarm);

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
            }
         }

         alarm_remover(next);
      }
   }
   status = pthread_mutex_unlock( & alarm_mutex);
   if (status != 0)
      err_abort(status, "unlock mutex");
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

alarm_t* create_alarm(unsigned int alarm_second, 
                      int message_type,
                      char request_type,
                      int message_number,
                      char* message)
{
   alarm_t *alarm;
   alarm = (alarm_t * ) malloc(sizeof(alarm_t));
   if (alarm == NULL)
      errno_abort("Allocate alarm");
  
   alarm->seconds = alarm_second;
   alarm->time = time(NULL) + alarm->seconds;
   alarm->message_type = message_type;
   alarm->status = 0;
   alarm->link = NULL;
   alarm->seen_by_alarm_thread = 0;
   alarm->alarm_request_type = request_type;
   alarm->message_number = message_number;
   if(message != NULL)
   {
      strcpy(alarm->message, message);
   }

   return alarm;
}

/**
 * Main Function, or Main thread
 * It is responible for get the command from input. It will first check the correction of command.
 * If the command it valid, it will insert it into alarm list, for other thread to read.
 * It also creates alarm thread to monitor the alarm thread.
 */
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
   
   sem_init(&semaphore_mutex, 0, 1);
   sem_init(&wrt, 0, 1);
   //Loop runs until terminated
   while (1) {
      printf("Alarm> ");

      display_thread_t * nextzs;
#ifdef DEBUG
      printf("[thread: ");
      for (nextzs = thread_list; nextzs != NULL; nextzs = nextzs->link)
         printf("%d :", nextzs->message_type);
      printf("]\n");
#endif
      alarm_t * temp_alarmzs;
      if (fgets(line, sizeof(line), stdin) == NULL) exit(0);
      if (strlen(line) <= 1) continue;

      //Get Command Type
      cmd_type = get_cmd_type(line, & message_type, & alarm_second, & message_number, message);
      switch (cmd_type) {
      case 1:   //Type B
         {
            //Does not exist any Type A alarm Request with same Message Type
            if (!alarm_list_containsmt(message_type, 'A')) {
               printf("Type B Alarm Request Error: No Alarm Request With Message Type (%d)!\n", message_type);
               break;
            }
            //IF typeB already exists
            if (alarm_list_containsmt(message_type, 'B')) {
               printf("Error: More Than One Type B Alarm Request With Message Type (%d)!\n", message_type);
               break;
            }

            alarm = create_alarm(0, message_type, 'B', 0, NULL);
            alarm_insert(alarm);

            printf("Type B Create Thread Alarm Request For Message Type (%d) Inserted Into Alarm List at %d!\n",
                   message_type, time(NULL));
            break;
         }
      case 2:   //Type C
         {
            //No Type A with same message_number
            if (!alarm_list_containsmn(message_number, 'A')) {
               printf("Error: No Alarm Request With Message Number (%d) to Cancel!\n", message_number);
               break;
            }

            if (alarm_list_containsmn(message_number, 'C')) {
               //Already a cancel request made
               printf("Error: More Than One Request to Cancel Alarm Request With Message Number (%d)!\n", message_number);
               break;
            }

            alarm = create_alarm(0, 0, 'C', message_number, NULL);
            alarm_insert(alarm);

            printf("Type C Cancel Alarm Request With Message Number (%d) Inserted Into Alarm List at %d: C\n", message_number, time(NULL));

            break;
         }
      case 3:   //Type A
         {
            //if type A with same message_number exists replace it
            if (alarm_list_containsmn(message_number, 'A')) {
               alarm_replace(message_number, alarm_second, message_type, message);
               printf("Type A Replacement Alarm Request With Message Number (%d) Inserted Into Alarm List at %d: A\n", message_number, time(NULL));

            } else {
            
               alarm = create_alarm(alarm_second, message_type, 'A', message_number, message);
               alarm_insert(alarm);
               printf("Type A Alarm Request With Message Number (%d) Inserted Into Alarm List at %d: A\n", alarm->message_number, time(NULL));
            }
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
