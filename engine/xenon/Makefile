# Xbox 360 build with the free60 toolchain (xenon-gcc + libxenon).
#   export DEVKITXENON=/usr/local/xenon
#   make -C engine/xenon
# Produces vsdemake.elf32, bootable from XeLL. Not verified yet: see
# notes/toolchain.md.
ifeq ($(strip $(DEVKITXENON)),)
$(error "set DEVKITXENON to the free60 toolchain root")
endif

include $(DEVKITXENON)/rules

TARGET := vsdemake
SRC := ../src
CORE := $(SRC)/core/vs_pack.c $(SRC)/core/vs_rng.c $(SRC)/core/vs_text.c $(SRC)/core/vs_audio.c $(SRC)/core/vs_vorbis.c \
        $(SRC)/game/game.c $(SRC)/game/timer.c $(SRC)/game/player.c $(SRC)/game/enemy.c \
        $(SRC)/game/stage.c $(SRC)/game/render.c $(SRC)/game/weapon.c $(SRC)/game/pickup.c \
        $(SRC)/game/levelup.c $(SRC)/game/tilemap.c $(SRC)/game/fx.c $(SRC)/game/save.c $(SRC)/game/options.c $(SRC)/game/treasure.c $(SRC)/game/shop.c $(SRC)/main.c
PLAT := $(SRC)/platform/xenon/vs_platform_xenon.c $(SRC)/platform/xenon/vs_xenon_shaders.c
OBJS := $(patsubst $(SRC)/%.c,build/%.o,$(CORE) $(PLAT))

CFLAGS := -O2 -g -Wall -std=gnu99 $(MACHDEP) -DVS_XENON=1 -DVS_BIG_ENDIAN=1 -I$(SRC) -I$(LIBXENON_INC)
LDFLAGS := -g $(MACHDEP) -Wl,--gc-sections -L$(LIBXENON_LIB)
LIBS := -lxenon -lfat -lm

all: $(TARGET).elf32

build/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $^ $(LDFLAGS) $(LIBS) -n -T $(DEVKITXENON)/app.lds -o $@

$(TARGET).elf32: $(TARGET).elf
	$(OBJCOPY) -O elf32-powerpc --adjust-vma 0x80000000 $< $@
	$(PREFIX)strip $@

clean:
	rm -rf build $(TARGET).elf $(TARGET).elf32

.PHONY: all clean
