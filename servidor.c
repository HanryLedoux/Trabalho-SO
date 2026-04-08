#include "banco.h"

// --- Banco de dados em memoria ---
Registro tabela[MAX_REGISTROS];
int total_registros = 0;

// Mutex basico para garantir exclusao mutua ao mexer no "banco"
pthread_mutex_t mutex_banco = PTHREAD_MUTEX_INITIALIZER;
// Mutex para evitar que as threads mandem mensagens ao mesmo tempo e embaralhem o Pipe de resposta
pthread_mutex_t mutex_resp = PTHREAD_MUTEX_INITIALIZER;

#ifdef _WIN32
HANDLE pipe_resp_global; // Referencia global para poder enviar as respostas
#else
int pipe_resp_global;
#endif

// --- Fila de requisicoes para as Threads do Pool ---
// Essa fila e o esquema de Produtor Consumidor (Main produz, threads consomem)
#define MAX_FILA 64
typedef struct {
    Requisicao reqs[MAX_FILA];
    int inicio, fim, tamanho;
    pthread_mutex_t mutex_fila;
    sem_t sem_tem_itens; // Quantos itens tem disponiveis pra pegar na fila
    sem_t sem_tem_espaco; // Quanto espaco resta na fila
} FilaTrabalho;

FilaTrabalho fila_pool;

void inicializa_fila() {
    fila_pool.inicio = 0;
    fila_pool.fim = 0;
    fila_pool.tamanho = 0;
    pthread_mutex_init(&fila_pool.mutex_fila, NULL);
    sem_init(&fila_pool.sem_tem_itens, 0, 0); // Começa vazio
    sem_init(&fila_pool.sem_tem_espaco, 0, MAX_FILA); // Comeca com todo espaco livre
}

void insere_fila(Requisicao r) {
    sem_wait(&fila_pool.sem_tem_espaco); // Espera ter espaco
    pthread_mutex_lock(&fila_pool.mutex_fila); // Trava pra manipular o vetor
    
    fila_pool.reqs[fila_pool.fim] = r;
    fila_pool.fim = (fila_pool.fim + 1) % MAX_FILA;
    fila_pool.tamanho++;
    
    pthread_mutex_unlock(&fila_pool.mutex_fila);
    sem_post(&fila_pool.sem_tem_itens); // Avisa que tem item novo disponivel
}

Requisicao retira_fila() {
    sem_wait(&fila_pool.sem_tem_itens); // Fica dormindo ate ter algo
    pthread_mutex_lock(&fila_pool.mutex_fila);
    
    Requisicao r = fila_pool.reqs[fila_pool.inicio];
    fila_pool.inicio = (fila_pool.inicio + 1) % MAX_FILA;
    fila_pool.tamanho--;
    
    pthread_mutex_unlock(&fila_pool.mutex_fila);
    sem_post(&fila_pool.sem_tem_espaco); // Libera espaco
    
    return r;
}

// Persistencia no TXT
void salvar_txt() {
    FILE* f = fopen("banco.txt", "w");
    if (!f) return;
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo) {
            fprintf(f, "ID: %d | NOME: %s\n", tabela[i].id, tabela[i].nome);
        }
    }
    fclose(f);
}

// Funcoes de resposta pelo pipe
void envia_resposta(Resposta resp) {
    pthread_mutex_lock(&mutex_resp);
#ifdef _WIN32
    DWORD escritos;
    WriteFile(pipe_resp_global, &resp, sizeof(Resposta), &escritos, NULL);
#else
    write(pipe_resp_global, &resp, sizeof(Resposta));
#endif
    pthread_mutex_unlock(&mutex_resp);
}

// --- Operacoes do Banco ---
void executar_insert(Requisicao r, Resposta *resp) {
    pthread_mutex_lock(&mutex_banco); // EXCLUSAO MUTUA

    // Busca duplicado primeiro
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo && tabela[i].id == r.id) {
            sprintf(resp->mensagem, "[ERRO] Ja existe registro com ID %d.", r.id);
            pthread_mutex_unlock(&mutex_banco);
            return;
        }
    }

    if (total_registros >= MAX_REGISTROS) {
        sprintf(resp->mensagem, "[ERRO] Banco lotado.");
    } else {
        tabela[total_registros].id = r.id;
        tabela[total_registros].ativo = 1;
        strcpy(tabela[total_registros].nome, r.nome);
        total_registros++;
        sprintf(resp->mensagem, "[INSERT] ID %d inserido com sucesso ('%s').", r.id, r.nome);
        salvar_txt();
    }

    pthread_mutex_unlock(&mutex_banco);
}

void executar_select(Requisicao r, Resposta *resp) {
    pthread_mutex_lock(&mutex_banco);
    
    int achou = 0;
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo && tabela[i].id == r.id) {
            sprintf(resp->mensagem, "[SELECT] Encontrado: ID=%d, Nome='%s'", tabela[i].id, tabela[i].nome);
            achou = 1;
            break;
        }
    }
    if (!achou) sprintf(resp->mensagem, "[ERRO] ID %d nao encontrado no banco.", r.id);

    pthread_mutex_unlock(&mutex_banco);
}

void executar_delete(Requisicao r, Resposta *resp) {
    pthread_mutex_lock(&mutex_banco);
    
    int achou = 0;
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo && tabela[i].id == r.id) {
            tabela[i].ativo = 0; // Exclusao logica simples
            sprintf(resp->mensagem, "[DELETE] ID %d removido.", r.id);
            achou = 1;
            salvar_txt();
            break;
        }
    }
    if (!achou) sprintf(resp->mensagem, "[ERRO] ID %d nao encontrado para remocao.", r.id);

    pthread_mutex_unlock(&mutex_banco);
}

void executar_update(Requisicao r, Resposta *resp) {
    pthread_mutex_lock(&mutex_banco);
    
    int achou = 0;
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo && tabela[i].id == r.id) {
            strcpy(tabela[i].nome, r.nome);
            sprintf(resp->mensagem, "[UPDATE] ID %d atualizado para '%s'.", r.id, r.nome);
            achou = 1;
            salvar_txt();
            break;
        }
    }
    if (!achou) sprintf(resp->mensagem, "[ERRO] ID %d nao encontrado para atualizacao.", r.id);

    pthread_mutex_unlock(&mutex_banco);
}

void executar_list(Requisicao r, Resposta *resp) {
    pthread_mutex_lock(&mutex_banco);
    
    strcpy(resp->mensagem, "[LIST] Registros Ativos:\n");
    int count = 0;
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo) {
            char linha[100];
            sprintf(linha, "  -> ID: %d | Nome: '%s'\n", tabela[i].id, tabela[i].nome);
            strcat(resp->mensagem, linha);
            count++;
        }
    }
    
    if (count == 0) {
        strcpy(resp->mensagem, "[LIST] O banco de dados esta vazio.");
    }
    
    pthread_mutex_unlock(&mutex_banco);
}

// --- Funcao que as threads executam ---
void* thread_trabalhadora(void* arg) {
    long id_thread = (long)arg;
    while(1) {
        Requisicao req = retira_fila(); // Bloqueia esperando trabalho

        if (req.tipo == OP_FIM) {
            // Repassa o aviso do fim adiante pras proximas threads!
            insere_fila(req);
            break;
        }

        Resposta resp;
        resp.id = req.id;
        
        printf("[Thread %ld] Processando ID %d\n", id_thread, req.id);

        if (req.tipo == OP_INSERT) executar_insert(req, &resp);
        else if(req.tipo == OP_SELECT) executar_select(req, &resp);
        else if(req.tipo == OP_DELETE) executar_delete(req, &resp);
        else if(req.tipo == OP_UPDATE) executar_update(req, &resp);
        else if(req.tipo == OP_LIST) executar_list(req, &resp);
        else sprintf(resp.mensagem, "[ERRO] Operacao invalida");

        // Envia resposta pelo PIPE pro cliente
        envia_resposta(resp);
    }
    return NULL;
}

int main() {
    printf("[Servidor] Iniciando o Servidor de Base de Dados (via Threads e Pipes)...\n");

    inicializa_fila();

    // Criando as threads do Pool
    pthread_t pool[NUM_THREADS];
    for(long i=0; i<NUM_THREADS; i++) {
        pthread_create(&pool[i], NULL, thread_trabalhadora, (void*)i);
    }

#ifdef _WIN32
    // Windows - Configurando Named Pipes
    HANDLE hPipeReq = CreateNamedPipeA(NOME_PIPE_REQ, PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 0, 0, 0, NULL);
    HANDLE hPipeResp = CreateNamedPipeA(NOME_PIPE_RESP, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 0, 0, 0, NULL);
    
    if (hPipeReq == INVALID_HANDLE_VALUE || hPipeResp == INVALID_HANDLE_VALUE) {
        printf("[Servidor] Erro ao criar o Pipe. Codigo de Erro: %lu\n", GetLastError());
        return 1;
    }
    
    pipe_resp_global = hPipeResp; // Salva para uso na thread
    
    printf("[Servidor] Aguardando o cliente conectar no pipe...\n");
    ConnectNamedPipe(hPipeReq, NULL);
    ConnectNamedPipe(hPipeResp, NULL);
#else
    // Linux - Configurando os FIFOs em /tmp
    mkfifo(NOME_PIPE_REQ, 0666);
    mkfifo(NOME_PIPE_RESP, 0666);

    printf("[Servidor] Aguardando o cliente conectar no pipe...\n");
    int fd_req = open(NOME_PIPE_REQ, O_RDONLY);
    int fd_resp = open(NOME_PIPE_RESP, O_WRONLY);
    
    if (fd_req < 0 || fd_resp < 0) {
        perror("[Servidor] Erro no pipe open");
        return 1;
    }
    pipe_resp_global = fd_resp;
#endif

    printf("[Servidor] Cliente se conectou! Comecando processamento...\n");

    // Loop que fica lendo as requisicoes do Pipe e distribuindo nas threads
    Requisicao r;
    while(1) {
        int bytes_lidos = 0;
#ifdef _WIN32
        DWORD lidos;
        if (ReadFile(hPipeReq, &r, sizeof(Requisicao), &lidos, NULL) && lidos == sizeof(Requisicao)) {
            bytes_lidos = lidos;
        }
#else
        bytes_lidos = read(fd_req, &r, sizeof(Requisicao));
#endif
        if (bytes_lidos <= 0) break; // Desconectou

        if (r.tipo == OP_FIM) {
            printf("[Servidor] Pedido de desligamento recebido do cliente.\n");
            insere_fila(r); // Acorda a galera para finalizar
            break;
        }

        // Coloca o pedido na fila (assincrono, as threads lidam depois)
        insere_fila(r);
    }

    // Esperando as threads irem pra casa
    for(int i=0; i<NUM_THREADS; i++) {
        pthread_join(pool[i], NULL);
    }

#ifdef _WIN32
    CloseHandle(hPipeReq);
    CloseHandle(hPipeResp);
#else
    close(fd_req);
    close(fd_resp);
    unlink(NOME_PIPE_REQ);
    unlink(NOME_PIPE_RESP);
#endif

    printf("[Servidor] Desligado com seguranca.\n");
    return 0;
}
