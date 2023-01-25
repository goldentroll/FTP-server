###############################################
# Makefile for compiling the program skeleton
# 'make' build executable file 'des'
# 'make clean' removes all .o
###############################################
PROJ = ftpserver # the name of the project
CC = gcc # name of compiler
# define any compile-time flags
CFLAGS = -lpthread -lcrypto -lssl # there is a space at the end of this
###############################################
# list of object files
# The following includes all of them!
C_FILES := $(wildcard *.c)
OBJS := $(patsubst %.c, %.o, $(C_FILES))
# To create the executable file we need the individual
# object files
$(PROJ): $(OBJS)
	$(CC) -g -o  $(PROJ) $(OBJS) $(LFLAGS) -lpthread -lcrypto -lssl -D_BSD_SOURCE
# To create each individual object file we need to
# compile these files using the following general
# purpose macro
.c.o:
	$(CC) $(CFLAGS) -g -c $<
# there is a TAB for each identation.
# To clean .o files: "make clean"
clean:
	rm -rf *.o
