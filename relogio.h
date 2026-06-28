#ifndef RELOGIO_H
#define RELOGIO_H

#include <pthread.h>

/*
 * Relógio global discreto (ticks).
 *
 * A thread do relógio incrementa "tick_atual" periodicamente e usa uma
 * variável de condição (cond_tick) para acordar todas as threads de
 * veículos que estejam dormindo esperando o próximo tick.
 *
 * Isso elimina a necessidade de espera ocupada: os carros chamam
 * relogio_esperar_proximo_tick(), que bloqueia em pthread_cond_wait()
 * até serem notificados.
 */

typedef struct {
    long tick_atual;
    int encerrar;                /* sinaliza fim da simulação */
    pthread_mutex_t mutex;
    pthread_cond_t  cond_tick;
    int duracao_tick_ms;         /* duração de cada tick, em milissegundos */
} Relogio;

/* Inicializa o relógio global. */
void relogio_inicializar(Relogio *rel, int duracao_tick_ms);

/* Libera recursos do relógio. */
void relogio_destruir(Relogio *rel);

/* Função executada pela thread do relógio (assinatura pthread). */
void *relogio_thread(void *arg);

/*
 * Bloqueia a thread chamadora até que um novo tick ocorra.
 * Usa pthread_cond_wait, portanto não consome CPU enquanto espera.
 * Retorna o número do tick em que a thread foi liberada.
 */
long relogio_esperar_proximo_tick(Relogio *rel, long ultimo_tick_visto);

/* Sinaliza para a thread do relógio (e demais) que a simulação deve terminar. */
void relogio_sinalizar_fim(Relogio *rel);

/* Retorna 1 se a simulação deve encerrar. */
int relogio_deve_encerrar(Relogio *rel);

/* Retorna o valor atual do tick de forma sincronizada (sob mutex). */
long relogio_obter_tick_atual(Relogio *rel);

#endif /* RELOGIO_H */
