# Makefile for compiling MySQL log plugin files
#--------------------------------------------------------------------------------

INCLUDES += -I/usr/include/mysql
LIBS += -L/usr/lib64/mysql/ -lmysqlclient `mysql_config --libs`
CFLAGS_SQL += `mysql_config --cflags`
CFLAGS += -std=gnu99 -g
CFLAGS += -D_GNU_SOURCE

all: sql_plugin

.PHONY: clean
clean:
	rm -f sql_plugin \
	rm -f *.o

sql_plugin: log_to_mysql.c log_receiver.c
	gcc log_to_mysql.c log_receiver.c  $(CFLAGS) $(CFLAGS_SQL) $(INCLUDES) $(LIBS) -o mistral_out_mysql_64

