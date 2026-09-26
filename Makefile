TOOLCHAIN ?= $(HOME)/.local/opt/cross-mint/usr/bin
CC        := $(TOOLCHAIN)/m68k-atari-mintelf-gcc

CFLAGS  := -O2 -Wall -Wextra -m68000
LDFLAGS := -s

# build/ is mounted as drive C: inside the emulator; obj/ holds intermediates
BUILD := build
OBJ   := obj

# BearSSL, built as a static library. The multiply settings tell it the
# 68000 has no fast 32x32 multiply (MULU.W is 16x16 and takes ~70 cycles).
BEARSSL     := third_party/bearssl
BEARSSL_CFLAGS := -O2 -m68000 -DBR_LOMUL=1 -DBR_SLOW_MUL=1 -DBR_SLOW_MUL15=1 \
                  -I$(BEARSSL)/inc -I$(BEARSSL)/src
BEARSSL_SRCS := $(shell find $(BEARSSL)/src -name '*.c')
BEARSSL_OBJS := $(patsubst $(BEARSSL)/src/%.c,$(OBJ)/bearssl/%.o,$(BEARSSL_SRCS))
BEARSSL_LIB  := $(OBJ)/libbearssl.a

TOS_ROM := emu/etos256us.img
SERIAL  := emu/serial

HATARI_FLAGS := --machine megaste --cpuclock 8 --memsize 4096 --tos $(TOS_ROM) --monitor mono \
                --harddrive $(BUILD) --gemdos-drive C \
                --fast-boot true --confirm-quit false --grab \
                --screenshot-dir screenshots \
                --rs232-in $(SERIAL) --rs232-out $(SERIAL)

PROGS := $(BUILD)/NETTEST.TOS $(BUILD)/TLSBENCH.TOS $(BUILD)/X25519T.TOS $(BUILD)/TLSTEST.TOS \
         $(BUILD)/RSATEST.TOS

# Programs that talk to the AI Gateway need .env (see .env.example).
CHAT_PROGS := $(BUILD)/ATCHAT.PRG $(BUILD)/CHAT.TOS

# X25519 with a hand-tuned (generated) assembly field multiply.
X25519_SRCS := src/x25519/fe16.c src/x25519/fe16_ref.c $(OBJ)/fe16_mul.S

.PHONY: all chat run clean compile_flags.txt

all: $(PROGS)

chat: $(CHAT_PROGS)


$(BUILD)/NETTEST.TOS: src/nettest.c src/serial.c src/serial.h | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/nettest.c src/serial.c

$(BUILD)/TLSBENCH.TOS: src/tlsbench.c src/serial.c src/serial.h $(BEARSSL_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BEARSSL)/inc $(LDFLAGS) -o $@ src/tlsbench.c src/serial.c $(BEARSSL_LIB)

$(BUILD)/X25519T.TOS: src/x25519test.c src/serial.c $(X25519_SRCS) src/x25519/fe16.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(LDFLAGS) -o $@ src/x25519test.c src/serial.c $(X25519_SRCS)

TLS_SRCS := src/serial.c src/modem.c src/tls/tls.c src/tls/deferred.c src/tls/entropy.c \
            src/x25519/ec_x25519.c src/x25519/fe16.c $(OBJ)/fe16_mul.S \
            src/rsa/rsa16.c src/rsa/addmul.S
TLS_HDRS := $(wildcard src/*.h src/tls/*.h src/x25519/*.h src/rsa/*.h)

$(BUILD)/RSATEST.TOS: src/rsatest.c src/serial.c src/rsa/rsa16.c src/rsa/addmul.S src/rsa/rsa16.h $(BEARSSL_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -I$(BEARSSL)/inc $(LDFLAGS) -o $@ src/rsatest.c src/serial.c src/rsa/rsa16.c src/rsa/addmul.S $(BEARSSL_LIB)

$(BUILD)/TLSTEST.TOS: src/tlstest.c $(TLS_SRCS) $(TLS_HDRS) $(BEARSSL_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -I$(BEARSSL)/inc $(LDFLAGS) -o $@ src/tlstest.c $(TLS_SRCS) $(BEARSSL_LIB)

# Credentials: .env (git-ignored) -> obj/config.h, never in src/.
CONFIG_H := $(OBJ)/config.h

$(CONFIG_H): .env tools/gen_config.py
	@mkdir -p $(OBJ)
	python3 tools/gen_config.py .env $@

CHAT_SRCS := $(TLS_SRCS) src/http.c src/json.c src/charset.c

$(BUILD)/CHAT.TOS: src/chattest.c $(CHAT_SRCS) $(TLS_HDRS) $(CONFIG_H) $(BEARSSL_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -I$(OBJ) -I$(BEARSSL)/inc $(LDFLAGS) -o $@ src/chattest.c $(CHAT_SRCS) $(BEARSSL_LIB)

APP_SRCS := src/app/main.c src/app/transcript.c src/app/claude.c src/menu.c $(CHAT_SRCS)

$(BUILD)/ATCHAT.PRG: $(APP_SRCS) $(wildcard src/app/*.h) src/menu.h $(TLS_HDRS) $(CONFIG_H) $(BEARSSL_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -Isrc/app -I$(OBJ) -I$(BEARSSL)/inc $(LDFLAGS) -o $@ $(APP_SRCS) $(BEARSSL_LIB) -lgem

$(OBJ)/fe16_mul.S: tools/gen_fe16.py
	@mkdir -p $(OBJ)
	python3 $< $@

$(OBJ)/bearssl/%.o: $(BEARSSL)/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(BEARSSL_CFLAGS) -c $< -o $@

$(BEARSSL_LIB): $(BEARSSL_OBJS)
	$(TOOLCHAIN)/m68k-atari-mintelf-ar rcs $@ $^

$(BUILD):
	mkdir -p $@

# Starts the modem emulator, runs Hatari, and stops the modem when Hatari exits.
# Builds the chat programs too when .env exists.
run: all $(if $(wildcard .env),chat)
	@rm -f $(SERIAL)
	@python3 tools/modem.py --link $(SERIAL) & MODEM=$$!; \
	trap 'kill $$MODEM 2>/dev/null' EXIT; \
	while [ ! -e $(SERIAL) ]; do sleep 0.1; done; \
	hatari $(HATARI_FLAGS)

clean:
	rm -rf $(BUILD) $(OBJ)

# Editor support: tells clangd where the Atari headers are.
TC_ROOT := $(abspath $(TOOLCHAIN)/..)
compile_flags.txt:
	@printf '%s\n' --target=m68k-unknown-none-elf -D__MINT__ -D__atarist__ -nostdinc \
	  -isystem $(TC_ROOT)/lib64/gcc/m68k-atari-mintelf/15/include \
	  -isystem $(TC_ROOT)/m68k-atari-mintelf/sys-root/usr/include \
	  -I$(BEARSSL)/inc -Isrc -Isrc/app -I$(OBJ) > $@
	@echo "wrote $@"
