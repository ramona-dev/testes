#ifndef SEMAFOROS_H
#define SEMAFOROS_H

#include <pthread.h>
#include <semaphore.h>
#include "mapa.h"

/*
 * Controle de sinais de trânsito por cruzamento.
 *
 * Cada cruzamento possui um estado de sinal para o eixo Norte-Sul (NS) e
 * para o eixo Leste-Oeste (LO). Quando um eixo está VERDE, o eixo
 * perpendicular está obrigatoriamente VERMELHO (segurança da transição).
 *
 * Mecanismos usados:
 *  - mutex: protege o estado do sinal (cor, contagem, prioridade de
 *    ambulância) contra acesso concorrente.
 *  - variável de condição: usada pelos carros para dormir até a via
 *    desejada ficar verde (sem espera ocupada).
 *  - semáforo (sem_t) por cruzamento: limita a quantidade de veículos
 *    simultaneamente "dentro" do cruzamento (capacidade limitada),
 *    evitando colisões na célula central mesmo com sinal verde.
 */

typedef enum {
    SINAL_VERMELHO = 0,
    SINAL_VERDE = 1
} CorSinal;

typedef enum {
    EIXO_NS = 0,  /* Norte-Sul   (vertical)   */
    EIXO_LO = 1   /* Leste-Oeste (horizontal) */
} EixoVia;

#define CAPACIDADE_CRUZAMENTO 1  /* nº de veículos simultâneos dentro do cruzamento */

typedef struct {
    int linha_cruz, col_cruz;     /* índice lógico do cruzamento na malha */
    CorSinal sinal[2];            /* sinal[EIXO_NS], sinal[EIXO_LO] */

    int ambulancia_solicitando;   /* >0 se há ambulância pedindo prioridade */
    int eixo_solicitado_ambulancia;

    pthread_mutex_t mutex;        /* protege o estado acima */
    pthread_cond_t  cond_sinal;   /* notifica troca de sinal */

    sem_t *sem_capacidade;        /* semáforo de capacidade do cruzamento */
} Cruzamento;

typedef struct {
    Cruzamento cruzamentos[MAPA_LINHAS_CRUZ][MAPA_COLS_CRUZ];
    int periodo_ticks;            /* ticks até alternância automática do sinal */
} ControleSemaforos;

/* Inicializa todos os cruzamentos com sinal inicial e capacidade. */
void semaforos_inicializar(ControleSemaforos *ctrl, int periodo_ticks);

/* Libera mutexes, condvars e semáforos. */
void semaforos_destruir(ControleSemaforos *ctrl);

/* Thread responsável por alternar os sinais periodicamente (a cada N ticks),
 * respeitando pedidos de prioridade de ambulância. */
void *semaforos_thread_controlador(void *arg);

/*
 * Bloqueia o veículo até que o sinal do cruzamento (linha_cruz,col_cruz)
 * para o eixo informado esteja VERDE. Usa variável de condição: dorme sem
 * consumir CPU. Retorna 0 se foi liberado por encerramento da simulação
 * (g_semaforos_encerrar) sem o sinal ter ficado verde, ou 1 se o sinal
 * está de fato verde.
 */
int semaforo_esperar_verde(Cruzamento *cruz, EixoVia eixo);

/* Ambulância solicita prioridade de passagem no cruzamento para um eixo. */
void semaforo_solicitar_prioridade_ambulancia(Cruzamento *cruz, EixoVia eixo);

/* Adquire uma "vaga" de capacidade dentro do cruzamento (sem_wait com
 * checagem periódica de encerramento, sem busy-waiting). Retorna 1 se
 * conseguiu entrar, 0 se desistiu por encerramento da simulação. */
int semaforo_entrar_cruzamento(Cruzamento *cruz);

/* Libera a vaga de capacidade do cruzamento (sem_post). */
void semaforo_sair_cruzamento(Cruzamento *cruz);

/* Acesso auxiliar a um cruzamento pela posição lógica. */
Cruzamento *semaforos_get_cruzamento(ControleSemaforos *ctrl, int linha_cruz, int col_cruz);

/*
 * Acorda explicitamente todas as threads de veículos que possam estar
 * bloqueadas em semaforo_esperar_verde, em qualquer cruzamento. Deve ser
 * chamado pelo main logo após sinalizar o encerramento da simulação
 * (g_semaforos_encerrar = 1), garantindo que nenhuma thread de veículo
 * fique presa indefinidamente em pthread_cond_wait quando o programa
 * está sendo finalizado.
 */
void semaforos_broadcast_encerramento(ControleSemaforos *ctrl);

/* Variável global para encerrar a thread controladora junto com a simulação.
 * Declarada como atomic_int (C11, <stdatomic.h>): operações de leitura e
 * escrita são atômicas e com ordenação de memória sequencialmente
 * consistente por padrão, o que é a forma correta e portável de
 * compartilhar uma flag booleana simples entre threads sem usar um mutex
 * completo, e é reconhecida como "synchronized" por ferramentas como o
 * ThreadSanitizer (evitando falsos positivos de data race). */
#include <stdatomic.h>
extern atomic_int g_semaforos_encerrar;

/* Retorna o tipo de via horizontal (dupla ou única) para uma linha de
 * cruzamento, conforme definido no mapa do projeto (Mapa.txt). */
TipoVia semaforos_tipo_via_horizontal(int linha_cruz);

#endif /* SEMAFOROS_H */
