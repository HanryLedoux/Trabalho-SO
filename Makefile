# ─── Makefile ─────────────────────────────────────────────────────────────────
CC      = gcc
ifeq ($(OS),Windows_NT)
    CFLAGS  = -Wall -Wextra -g
else
    CFLAGS  = -Wall -Wextra -g -pthread
endif

TARGETS = servidor cliente

all: $(TARGETS)

servidor: servidor.c banco.h
	$(CC) $(CFLAGS) -o servidor servidor.c

cliente: cliente.c banco.h
	$(CC) $(CFLAGS) -o cliente cliente.c

# Roda servidor em background e depois o cliente
run: all
	@echo ">>> Iniciando servidor em background..."
	./servidor &
	@sleep 1
	@echo ">>> Iniciando cliente..."
	./cliente
	@wait

clean:
	rm -f servidor cliente banco.log

.PHONY: all run clean
