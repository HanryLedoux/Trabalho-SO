#ifndef BANCO_H
#define BANCO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    // Nomes dos PIPES para comunicação no Windows
    #define NOME_PIPE_REQ   "\\\\.\\pipe\\banco_req"
    #define NOME_PIPE_RESP  "\\\\.\\pipe\\banco_resp"
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <pthread.h>
    #include <semaphore.h>
    // Nomes dos PIPES no Linux (FIFOs criados no /tmp)
    #define NOME_PIPE_REQ   "/tmp/banco_req"
    #define NOME_PIPE_RESP  "/tmp/banco_resp"
#endif

// --- Configurações Básicas ---
#define MAX_REGISTROS 200
#define MAX_NOME 50
#define NUM_THREADS 4

// --- Estruturas Auxiliares ---
typedef struct {
    int id;
    char nome[MAX_NOME];
    int ativo; // 1 = cadastro ativo, 0 = excluido logicalmente
} Registro;

// Tipos de comandos possíveis
typedef enum {
    OP_INSERT = 1,
    OP_SELECT,
    OP_DELETE,
    OP_UPDATE,
    OP_FIM  // Finaliza o servidor
} TipoOperacao;

// Estrutura enviada do Cliente para o Servidor (pelo Pipe Req)
typedef struct {
    TipoOperacao tipo;
    int id;
    char nome[MAX_NOME]; 
} Requisicao;

// Estrutura retornada do Servidor para o Cliente (pelo Pipe Resp)
typedef struct {
    int id;
    char mensagem[256];
} Resposta;

#endif // BANCO_H
