all:app

CC = gcc

INSTDIR = ./out

# Options for development
# CFLAGS = -g -Wall -std=c99 -pedantic -rdynamic
  CFLAGS = -g -Wall -std=gnu99 -rdynamic

# Options for release
# CFLAGS = -O -Wall -std=c99 -pedantic 


LIB  = epdlib.a


#Tool command
clean:
	-rm app $(TEST) *.o $(LIB)

uninstall:clean
	-rm -rf $(INSTDIR)

install:app
ifeq ($(INSTDIR),$(wildcard $(INSTDIR)))
	@echo "Sorry , $(INSTDIR) does exist . Please uninstall.";
else
	@mkdir $(INSTDIR); \
	cp app $(INSTDIR);
endif
