all:app

CC = gcc

INSTDIR = ./out

# Options for development
# CFLAGS = -g -Wall -std=c99 -pedantic -rdynamic
  CFLAGS = -g -Wall -std=gnu99 -rdynamic

# Options for release
# CFLAGS = -O -Wall -std=c99 -pedantic 

LIB  = epdlib.a

# app vars
_PED_C = $(wildcard ./p*.c)
PED_C  = $(notdir $(_PED_C))
PED_O  = $(patsubst %.c,%.o,$(PED_C))
PED    = $(patsubst %.c,%,$(PED_C))

app: app.o $(LIB)
	$(CC) $(CFLAGS) -o app app.o $(LIB)

$(LIB):   $(LIB)($(PED_O))
app.o:    app.c
$(PED_O): $(PED_C)

#Tool command
echo:
	@echo " $(_PED_C) --- $(PED_C) --- $(PED_O) --- $(PED)"

clean:
	-rm app  *.o $(LIB)
