HAVE_FILE_LOGGER=1

include config.mk

TARGET = retroarch
JTARGET = tools/retroarch-joyconfig 

OBJDIR := obj-unix

ifeq ($(GLOBAL_CONFIG_DIR),)
   GLOBAL_CONFIG_DIR = /etc
endif

OBJ := 
JOYCONFIG_OBJ :=
LIBS :=
DEFINES := -DHAVE_CONFIG_H -DRARCH_INTERNAL -DHAVE_OVERLAY
DEFINES += -DGLOBAL_CONFIG_DIR='"$(GLOBAL_CONFIG_DIR)"'

ifneq ($(findstring Win32,$(OS)),)
   LDFLAGS += -static-libgcc
endif

include Makefile.common

HEADERS = $(wildcard */*/*.h) $(wildcard */*.h) $(wildcard *.h)

ifeq ($(HAVE_DYLIB), 1)
   LIBS += $(DYLIB_LIB)
endif

ifeq ($(HAVE_DYNAMIC), 1)
   LIBS += $(DYLIB_LIB)
else
   LIBS += $(libretro)
endif

ifneq ($(V),1)
   Q := @
endif

ifeq ($(DEBUG), 1)
   OPTIMIZE_FLAG = -O0 -g
else
   OPTIMIZE_FLAG = -O3 -ffast-math
endif

CFLAGS   += -Wall $(OPTIMIZE_FLAG) $(INCLUDE_DIRS) $(DEBUG_FLAG) -I.
CXXFLAGS := $(CFLAGS) -std=c++0x -D__STDC_CONSTANT_MACROS
OBJCFLAGS :=  $(CFLAGS) -D__STDC_CONSTANT_MACROS

ifeq ($(CXX_BUILD), 1)
   LINK = $(CXX)
   CFLAGS := $(CXXFLAGS) -xc++
else
   ifeq ($(findstring Win32,$(OS)),)
      LINK = $(CC)
   else
      # directx-related code is c++
      LINK = $(CXX)
   endif

   ifneq ($(GNU90_BUILD), 1)
      ifneq ($(findstring icc,$(CC)),)
         CFLAGS += -std=c99 -D_GNU_SOURCE
      else
         CFLAGS += -std=gnu99 -D_GNU_SOURCE
      endif
   endif

   ifneq ($(C89_BUILD)$(C90_BUILD),)
   #looks kinda ugly, but it makes both C89_BUILD and C90_BUILD work and refer to the same thing
      CFLAGS += -std=c89 -ansi -pedantic -Werror=pedantic
   endif
endif

ifeq ($(NOUNUSED), yes)
   CFLAGS += -Wno-unused-result
   CXXFLAGS += -Wno-unused-result
endif
ifeq ($(NOUNUSED_VARIABLE), yes)
   CFLAGS += -Wno-unused-variable
   CXXFLAGS += -Wno-unused-variable
endif

RARCH_OBJ := $(addprefix $(OBJDIR)/,$(OBJ))
RARCH_JOYCONFIG_OBJ := $(addprefix $(OBJDIR)/,$(JOYCONFIG_OBJ))

ifneq ($(SANITIZER),)
    CFLAGS   := -fsanitize=$(SANITIZER) $(CFLAGS)
    CXXFLAGS := -fsanitize=$(SANITIZER) $(CXXFLAGS)
    LDFLAGS  := -fsanitize=$(SANITIZER) $(LDLAGS)
endif

all: $(TARGET) $(JTARGET) config.mk

-include $(RARCH_OBJ:.o=.d) $(RARCH_JOYCONFIG_OBJ:.o=.d)
config.mk: configure qb/*
	@echo "config.mk is outdated or non-existing. Run ./configure again."
	@exit 1

retroarch: $(RARCH_OBJ)
	@$(if $(Q), $(shell echo echo LD $@),)
	$(Q)$(LINK) -o $@ $(RARCH_OBJ) $(LIBS) $(LDFLAGS) $(LIBRARY_DIRS)

$(JTARGET): $(RARCH_JOYCONFIG_OBJ)
	@$(if $(Q), $(shell echo echo LD $@),)
	$(Q)$(LINK) -o $@ $(RARCH_JOYCONFIG_OBJ) $(JOYCONFIG_LIBS) $(LDFLAGS) $(LIBRARY_DIRS)

$(OBJDIR)/%.o: %.c config.h config.mk
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo CC $<),)
	$(Q)$(CC) $(CFLAGS) $(DEFINES) -MMD -c -o $@ $<

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo CXX $<),)
	$(Q)$(CXX) $(CXXFLAGS) $(DEFINES) -MMD -c -o $@ $<

$(OBJDIR)/%.o: %.m
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo OBJC $<),)
	$(Q)$(CXX) $(OBJCFLAGS) $(DEFINES) -MMD -c -o $@ $<

.FORCE:

$(OBJDIR)/git_version.o: git_version.c .FORCE
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo CC $<),)
	$(Q)$(CC) $(CFLAGS) $(DEFINES) -MMD -c -o $@ $<

$(OBJDIR)/tools/linuxraw_joypad.o: input/linuxraw_joypad.c
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo CC $<),)
	$(Q)$(CC) $(CFLAGS) $(DEFINES) -MMD -DIS_JOYCONFIG -c -o $@ $<

$(OBJDIR)/tools/parport_joypad.o: input/parport_joypad.c
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo CC $<),)
	$(Q)$(CC) $(CFLAGS) $(DEFINES) -MMD -DIS_JOYCONFIG -c -o $@ $<

$(OBJDIR)/tools/udev_joypad.o: input/udev_joypad.c
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo CC $<),)
	$(Q)$(CC) $(CFLAGS) $(DEFINES) -MMD -DIS_JOYCONFIG -c -o $@ $<

$(OBJDIR)/%.o: %.S config.h config.mk $(HEADERS)
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo AS $<),)
	$(Q)$(CC) $(CFLAGS) $(ASFLAGS) $(DEFINES) -c -o $@ $<

$(OBJDIR)/%.o: %.rc $(HEADERS)
	@mkdir -p $(dir $@)
	@$(if $(Q), $(shell echo echo WINDRES $<),)
	$(Q)$(WINDRES) -o $@ $<

install: $(TARGET)
	rm -f $(OBJDIR)/git_version.o
	mkdir -p $(DESTDIR)$(PREFIX)/bin 2>/dev/null || /bin/true
	mkdir -p $(DESTDIR)$(GLOBAL_CONFIG_DIR) 2>/dev/null || /bin/true
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1 2>/dev/null || /bin/true
	mkdir -p $(DESTDIR)$(PREFIX)/share/pixmaps 2>/dev/null || /bin/true
	install -m755 $(TARGET) $(DESTDIR)$(PREFIX)/bin 
	install -m755 tools/cg2glsl.py $(DESTDIR)$(PREFIX)/bin/retroarch-cg2glsl
	install -m755 $(JTARGET) $(DESTDIR)$(PREFIX)/bin
	install -m644 retroarch.cfg $(DESTDIR)$(GLOBAL_CONFIG_DIR)/retroarch.cfg
	install -m644 docs/retroarch.1 $(DESTDIR)$(MAN_DIR)
	install -m644 docs/retroarch-cg2glsl.1 $(DESTDIR)$(MAN_DIR)
	install -m644 docs/retroarch-joyconfig.1 $(DESTDIR)$(MAN_DIR)
	install -m644 media/retroarch.png $(DESTDIR)$(PREFIX)/share/pixmaps
	install -m644 media/retroarch.svg $(DESTDIR)$(PREFIX)/share/pixmaps

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/retroarch
	rm -f $(DESTDIR)$(PREFIX)/bin/retroarch-joyconfig
	rm -f $(DESTDIR)$(PREFIX)/bin/retroarch-cg2glsl
	rm -f $(DESTDIR)$(GLOBAL_CONFIG_DIR)/retroarch.cfg
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/retroarch.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/retroarch-cg2glsl.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/retroarch-joyconfig.1
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/retroarch.png
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/retroarch.svg

clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET)
	rm -f $(JTARGET)
	rm -f *.d

.PHONY: all install uninstall clean
