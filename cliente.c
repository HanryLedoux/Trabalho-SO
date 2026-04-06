#include "banco.h"

// Helpers basicos para agilizar a formatacao
Requisicao criar_insert(int id, const char* nome) {
    Requisicao r = {OP_INSERT, id, ""};
    strcpy(r.nome, nome);
    return r;
}

Requisicao criar_select(int id) {
    Requisicao r = {OP_SELECT, id, ""};
    return r;
}

Requisicao criar_delete(int id) {
    Requisicao r = {OP_DELETE, id, ""};
    return r;
}

Requisicao criar_update(int id, const char* novo_nome) {
    Requisicao r = {OP_UPDATE, id, ""};
    strcpy(r.nome, novo_nome);
    return r;
}

// Funcao auxiliar para mandar
void envia_pro_servidor(
#ifdef _WIN32
    HANDLE fd, 
#else
    int fd, 
#endif
    Requisicao r, const char* descricao
) {
    printf("[Cliente] Solicitando: %s\n", descricao);
#ifdef _WIN32
    DWORD escritos;
    WriteFile(fd, &r, sizeof(Requisicao), &escritos, NULL);
    Sleep(50); // Pause basico de estudante pra ver os logs devagar
#else
    write(fd, &r, sizeof(Requisicao));
    usleep(50000); 
#endif
}

int main() {
    printf("[Cliente] Iniciando cliente. Conectando no Servidor via PIPE...\n");

#ifdef _WIN32
    // Windows - abrimos arquivo comendo nome do Pipe como se fosse arquivo
    HANDLE hPipeReq = CreateFileA(NOME_PIPE_REQ, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE hPipeResp = CreateFileA(NOME_PIPE_RESP, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    
    if (hPipeReq == INVALID_HANDLE_VALUE || hPipeResp == INVALID_HANDLE_VALUE) {
        printf("[Cliente] Nao achei o pipe do servidor. Ele ta rodando?\n");
        return 1;
    }
#else
    // Linux - Abre arquivo
    int fd_req = open(NOME_PIPE_REQ, O_WRONLY);
    int fd_resp = open(NOME_PIPE_RESP, O_RDONLY);
    if (fd_req < 0 || fd_resp < 0) {
        perror("[Cliente] Nao achei o pipe do servidor");
        return 1;
    }
#endif

    printf("[Cliente] Conectado e pronto. Disparando os testes!\n\n");

    // Lote de testes (bem misturado para testar se funciona mesmo nas threads)
    Requisicao testes[] = {
        criar_insert(1, "Alice"),
        criar_insert(2, "Bruno"),
        criar_insert(3, "Carlos"),
        criar_insert(4, "Diego"),
        criar_insert(5, "Eduardo"),
        criar_select(1),
        criar_select(90),     // Nao deve achar nada
        criar_update(3, "Carlos Magno"),
        criar_select(3),      // Deve ver alterado
        criar_delete(2),
        criar_select(2),      // Removido
        criar_insert(1, "Erro Duplicado") // Deve rejeitar
    };
    
    int quantidade = sizeof(testes)/sizeof(Requisicao);

    // Enviar todas em sequencia
    char msg[100];
    for (int i=0; i<quantidade; i++) {
        if (testes[i].tipo == OP_INSERT) sprintf(msg, "INSERT ID %d", testes[i].id);
        else if (testes[i].tipo == OP_SELECT) sprintf(msg, "SELECT ID %d", testes[i].id);
        else if (testes[i].tipo == OP_UPDATE) sprintf(msg, "UPDATE ID %d", testes[i].id);
        else if (testes[i].tipo == OP_DELETE) sprintf(msg, "DELETE ID %d", testes[i].id);
        
#ifdef _WIN32
        envia_pro_servidor(hPipeReq, testes[i], msg);
#else
        envia_pro_servidor(fd_req, testes[i], msg);
#endif
    }

    printf("\n[Cliente] ==== Lendo as Respostas! ====\n");
    Resposta resp;
    for (int i=0; i<quantidade; i++) {
#ifdef _WIN32
        DWORD lidos;
        if (ReadFile(hPipeResp, &resp, sizeof(Resposta), &lidos, NULL) && lidos > 0) {
            printf(" <- %s\n", resp.mensagem);
        }
#else
        if (read(fd_resp, &resp, sizeof(Resposta)) > 0) {
            printf(" <- %s\n", resp.mensagem);
        }
#endif
    }
    
    // Agora informando ao servidor para desligar
    Requisicao fim = {OP_FIM, 0, ""};
#ifdef _WIN32
    DWORD escritos;
    WriteFile(hPipeReq, &fim, sizeof(Requisicao), &escritos, NULL);
    CloseHandle(hPipeReq);
    CloseHandle(hPipeResp);
#else
    write(fd_req, &fim, sizeof(Requisicao));
    close(fd_req);
    close(fd_resp);
#endif

    printf("\n[Cliente] Finalizado!\n");
    return 0;
}
