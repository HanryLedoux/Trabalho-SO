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

Requisicao criar_list() {
    Requisicao r = {OP_LIST, 0, ""};
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

    printf("[Cliente] Conectado e pronto. Entrando no modo interativo!\n\n");

    while (1) {
        int opcao = -1;
        printf("\n================ OPTIONS ================\n");
        printf("1. INSERT (Criar novo registro)\n");
        printf("2. SELECT (Buscar registro por ID)\n");
        printf("3. UPDATE (Atualizar nome do registro)\n");
        printf("4. DELETE (Remover registro por ID)\n");
        printf("5. LIST (Mostrar todos os registros)\n");
        printf("0. SAIR\n");
        printf("=========================================\n");
        printf("Escolha uma operacao: ");
        
        if (scanf("%d", &opcao) != 1) {
            // Limpa o buffer em caso de entrada errada
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            continue;
        }

        if (opcao == 0) {
            break;
        }

        Requisicao req;
        char msg[100];
        
        if (opcao == 1) { // INSERT
            int id;
            char nome[MAX_NOME];
            printf("Digite o ID: ");
            scanf("%d", &id);
            printf("Digite o Nome: ");
            scanf(" %[^\n]s", nome);
            req = criar_insert(id, nome);
            sprintf(msg, "INSERT ID %d '%s'", id, nome);
        }
        else if (opcao == 2) { // SELECT
            int id;
            printf("Digite o ID para buscar: ");
            scanf("%d", &id);
            req = criar_select(id);
            sprintf(msg, "SELECT ID %d", id);
        }
        else if (opcao == 3) { // UPDATE
            int id;
            char novo_nome[MAX_NOME];
            printf("Digite o ID para atualizar: ");
            scanf("%d", &id);
            printf("Digite o Novo Nome: ");
            scanf(" %[^\n]s", novo_nome);
            req = criar_update(id, novo_nome);
            sprintf(msg, "UPDATE ID %d '%s'", id, novo_nome);
        }
        else if (opcao == 4) { // DELETE
            int id;
            printf("Digite o ID para remover: ");
            scanf("%d", &id);
            req = criar_delete(id);
            sprintf(msg, "DELETE ID %d", id);
        }
        else if (opcao == 5) { // LIST
            req = criar_list();
            sprintf(msg, "LIST");
        }
        else {
            printf("[Erro] Opcao invalida. Tente novamente.\n");
            continue;
        }

#ifdef _WIN32
        envia_pro_servidor(hPipeReq, req, msg);
#else
        envia_pro_servidor(fd_req, req, msg);
#endif

        Resposta resp;
#ifdef _WIN32
        DWORD lidos;
        if (ReadFile(hPipeResp, &resp, sizeof(Resposta), &lidos, NULL) && lidos > 0) {
            printf(" <- %s\n", resp.mensagem);
        } else {
            printf(" [!] Erro ao ler resposta do servidor.\n");
        }
#else
        if (read(fd_resp, &resp, sizeof(Resposta)) > 0) {
            printf(" <- %s\n", resp.mensagem);
        } else {
            printf(" [!] Erro ao ler resposta do servidor.\n");
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
