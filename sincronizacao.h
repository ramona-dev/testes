#ifndef SINCRONIZACAO_H
#define SINCRONIZACAO_H

#include <pthread.h>

/*
 * Módulo de sincronização auxiliar.
 *
 * Centraliza:
 *  - mutex global de log/console, para que a thread de visualização e as
 *    mensagens de eventos (ex.: pedido de prioridade da ambulância) não
 *    se intercalem de forma corrompida no terminal.
 *  - funções utilitárias de log thread-safe.
 *  - estratégia de prevenção de deadlock: ordenação fixa de aquisição de
 *    recursos (ver sincronizacao.c e relatorio.md, seção 8).
 */

extern pthread_mutex_t g_mutex_log;

/* Inicializa mutexes globais de sincronização. Deve ser chamado uma vez
 * no início do programa, antes de criar qualquer thread. */
void sincronizacao_inicializar(void);

/* Libera mutexes globais. Deve ser chamado após o join de todas as threads. */
void sincronizacao_destruir(void);

/* Log thread-safe: serializa escritas no terminal/relatório de eventos. */
void log_evento(const char *formato, ...);

/*
 * Aplica a estratégia de prevenção de deadlock para aquisição de duas
 * células (origem e destino) em movimentos que cruzam o cruzamento.
 *
 * Estratégia: ORDEM FIXA DE AQUISIÇÃO. As células são sempre travadas em
 * ordem crescente de (linha, coluna) — primeiro a célula de menor índice
 * (linha*MAPA_COLS+col), depois a de maior índice. Isso impede a espera
 * circular (circular wait) entre dois veículos que tentam trocar de
 * lugar/cruzar trajetórias simultaneamente, pois ambos tentarão adquirir
 * os mutexes na mesma ordem global.
 *
 * Retorna 1 se conseguiu adquirir ambos os mutexes na ordem definida,
 * preenchendo *primeiro_e_origem para indicar qual mutex foi travado
 * primeiro (apenas informativo/log).
 */
void travar_em_ordem(pthread_mutex_t *mutexA, int idA,
                      pthread_mutex_t *mutexB, int idB);
void destravar_em_ordem(pthread_mutex_t *mutexA, int idA,
                         pthread_mutex_t *mutexB, int idB);

#endif /* SINCRONIZACAO_H */
