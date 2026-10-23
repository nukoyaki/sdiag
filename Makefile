CC      ?= gcc
.DEFAULT_GOAL := all
BUILD   := build
GEN     := $(BUILD)/gen

WL_PROTOCOLS := /usr/share/wayland-protocols
WL_XML       := /usr/share/wayland/wayland.xml

CFLAGS  ?= -Os
CFLAGS  += -std=c23 -Wall -Wextra -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections \
           -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
           -Isrc -Ivendor/qoi -I$(GEN)
LDFLAGS += -Wl,--gc-sections -Wl,--build-id=none -s

GEN_H :=
GEN_C := $(GEN)/wayland-protocol.c

define proto
$(GEN)/$(1)-client-protocol.h: $(2) | $(GEN)
	wayland-scanner client-header $$< $$@
$(GEN)/$(1)-protocol.c: $(2) | $(GEN)
	wayland-scanner private-code $$< $$@
GEN_H += $(GEN)/$(1)-client-protocol.h
GEN_C += $(GEN)/$(1)-protocol.c
endef

$(eval $(call proto,xdg-shell,$(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml))
$(eval $(call proto,viewporter,$(WL_PROTOCOLS)/stable/viewporter/viewporter.xml))
$(eval $(call proto,fractional-scale-v1,$(WL_PROTOCOLS)/staging/fractional-scale/fractional-scale-v1.xml))
$(eval $(call proto,cursor-shape-v1,$(WL_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml))

$(GEN)/wayland-protocol.c: $(WL_XML) | $(GEN)
	wayland-scanner private-code $< $@

LIB_SRC := atlas font paint ui sdiag platform_x11 platform_wayland
LIB_OBJ := $(LIB_SRC:%=$(BUILD)/%.o) $(GEN_C:$(GEN)/%.c=$(BUILD)/gen_%.o)

all: $(BUILD)/libsdiag.a $(BUILD)/sdiag

$(BUILD)/libsdiag.a: $(LIB_OBJ)
	ar rcs $@ $^

$(BUILD)/sdiag: $(BUILD)/cli.o $(BUILD)/libsdiag.a
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gen_%.o: $(GEN)/%.c
	$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/atlas.o: resources/icons.qoi
$(BUILD)/font.o: resources/font.raw
$(BUILD)/platform_wayland.o: $(GEN_H)
$(BUILD)/%.o: src/*.h

$(BUILD) $(GEN):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
