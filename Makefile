a3: New_Alarm_Cond.o
	cc -lpthread -o a3 New_Alarm_Cond.o

New_Alarm_Cond.o: New_Alarm_Cond.c
	cc -D DEBUG -c -g New_Alarm_Cond.c -D_POSIX_PTHREAD_SEMANTICS 
