#include "banco.h"

// Definicao das Macros Abstratas para Threads Nativas / POSIX
#ifdef _WIN32
  #include <windows.h>
  typedef HANDLE MY_MUTEX_T;
  typedef HANDLE MY_SEM_T;
  typedef HANDLE MY_THREAD_T;
  #define MUTEX_INIT(m) do { (m) = CreateMutex(NULL, FALSE, NULL); } while(0)
  #define MUTEX_LOCK(m) WaitForSingleObject((m), INFINITE)
  #define MUTEX_UNLOCK(m) ReleaseMutex(m)
  
  #define SEM_INIT(s, val) do { (s) = CreateSemaphore(NULL, (val), 999999, NULL); } while(0)
  #define SEM_WAIT(s) WaitForSingleObject((s), INFINITE)
  #define SEM_POST(s) ReleaseSemaphore((s), 1, NULL)
  
  #define THREAD_CREATE(t, func, arg) do { \
      (t) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(func), (LPVOID)(arg), 0, NULL); \
  } while(0)
  #define THREAD_JOIN(t) do { WaitForSingleObject((t), INFINITE); CloseHandle(t); } while(0)
  #define THREAD_RET DWORD WINAPI
#else
  typedef pthread_mutex_t MY_MUTEX_T;
  typedef sem_t MY_SEM_T;
  typedef pthread_t MY_THREAD_T;
  #define MUTEX_INIT(m) pthread_mutex_init(&(m), NULL)
  #define MUTEX_LOCK(m) pthread_mutex_lock(&(m))
  #define MUTEX_UNLOCK(m) pthread_mutex_unlock(&(m))
  
  #define SEM_INIT(s, val) sem_init(&(s), 0, (val))
  #define SEM_WAIT(s) sem_wait(&(s))
  #define SEM_POST(s) sem_post(&(s))
  
  #define THREAD_CREATE(t, func, arg) pthread_create(&(t), NULL, (func), (arg))
  #define THREAD_JOIN(t) pthread_join((t), NULL)
  #define THREAD_RET void*
#endif

// --- Banco de dados em memoria ---
Registro tabela[MAX_REGISTROS];
int total_registros = 0;

// Mutex basico para garantir exclusao mutua ao mexer no "banco"
MY_MUTEX_T mutex_banco;
// Mutex para evitar que as threads mandem mensagens ao mesmo tempo e embaralhem o Pipe de resposta
MY_MUTEX_T mutex_resp;

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
    MY_MUTEX_T mutex_fila;
    MY_SEM_T sem_tem_itens; // Quantos itens tem disponiveis pra pegar na fila
    MY_SEM_T sem_tem_espaco; // Quanto espaco resta na fila
} FilaTrabalho;

FilaTrabalho fila_pool;

void inicializa_fila() {
    fila_pool.inicio = 0;
    fila_pool.fim = 0;
    fila_pool.tamanho = 0;
    MUTEX_INIT(fila_pool.mutex_fila);
    SEM_INIT(fila_pool.sem_tem_itens, 0); // Começa vazio
    SEM_INIT(fila_pool.sem_tem_espaco, MAX_FILA); // Comeca com todo espaco livre
}

void insere_fila(Requisicao r) {
    SEM_WAIT(fila_pool.sem_tem_espaco); // Espera ter espaco
    MUTEX_LOCK(fila_pool.mutex_fila); // Trava pra manipular o vetor
    
    fila_pool.reqs[fila_pool.fim] = r;
    fila_pool.fim = (fila_pool.fim + 1) % MAX_FILA;
    fila_pool.tamanho++;
    
    MUTEX_UNLOCK(fila_pool.mutex_fila);
    SEM_POST(fila_pool.sem_tem_itens); // Avisa que tem item novo disponivel
}

Requisicao retira_fila() {
    SEM_WAIT(fila_pool.sem_tem_itens); // Fica dormindo ate ter algo
    MUTEX_LOCK(fila_pool.mutex_fila);
    
    Requisicao r = fila_pool.reqs[fila_pool.inicio];
    fila_pool.inicio = (fila_pool.inicio + 1) % MAX_FILA;
    fila_pool.tamanho--;
    
    MUTEX_UNLOCK(fila_pool.mutex_fila);
    SEM_POST(fila_pool.sem_tem_espaco); // Libera espaco
    
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
    MUTEX_LOCK(mutex_resp);
#ifdef _WIN32
    DWORD escritos;
    WriteFile(pipe_resp_global, &resp, sizeof(Resposta), &escritos, NULL);
#else
    write(pipe_resp_global, &resp, sizeof(Resposta));
#endif
    MUTEX_UNLOCK(mutex_resp);
}

// --- Operacoes do Banco ---
void executar_insert(Requisicao r, Resposta *resp) {
    MUTEX_LOCK(mutex_banco); // EXCLUSAO MUTUA

    // Busca duplicado primeiro
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo && tabela[i].id == r.id) {
            sprintf(resp->mensagem, "[ERRO] Ja existe registro com ID %d.", r.id);
            MUTEX_UNLOCK(mutex_banco);
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

    MUTEX_UNLOCK(mutex_banco);
}

void executar_select(Requisicao r, Resposta *resp) {
    MUTEX_LOCK(mutex_banco);
    
    int achou = 0;
    for (int i = 0; i < total_registros; i++) {
        if (tabela[i].ativo && tabela[i].id == r.id) {
            sprintf(resp->mensagem, "[SELECT] Encontrado: ID=%d, Nome='%s'", tabela[i].id, tabela[i].nome);
            achou = 1;
            break;
        }
    }
    if (!achou) sprintf(resp->mensagem, "[ERRO] ID %d nao encontrado no banco.", r.id);

    MUTEX_UNLOCK(mutex_banco);
}

void executar_delete(Requisicao r, Resposta *resp) {
    MUTEX_LOCK(mutex_banco);
    
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

    MUTEX_UNLOCK(mutex_banco);
}

void executar_update(Requisicao r, Resposta *resp) {
    MUTEX_LOCK(mutex_banco);
    
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

    MUTEX_UNLOCK(mutex_banco);
}


// --- Funcao que as threads executam ---
THREAD_RET thread_trabalhadora(void* arg) {
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
        else sprintf(resp.mensagem, "[ERRO] Operacao invalida");

        // Envia resposta pelo PIPE pro cliente
        envia_resposta(resp);
    }
    return 0;
}

int main() {
    printf("[Servidor] Iniciando o Servidor de Base de Dados (via Threads e Pipes)...\n");

    MUTEX_INIT(mutex_banco);
    MUTEX_INIT(mutex_resp);

    inicializa_fila();

    // Criando as threads do Pool
    MY_THREAD_T pool[NUM_THREADS];
    for(long i=0; i<NUM_THREADS; i++) {
        THREAD_CREATE(pool[i], thread_trabalhadora, (void*)i);
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
        THREAD_JOIN(pool[i]);
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
