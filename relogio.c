#define _POSIX_C_SOURCE 200809L
#include "relogio.h"
#include <stdio.h>
#include <time.h>

static void dormir_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void relogio_inicializar(Relogio *rel, int duracao_tick_ms) {
    rel->tick_atual = 0;
    rel->encerrar = 0;
    rel->duracao_tick_ms = duracao_tick_ms;
    pthread_mutex_init(&rel->mutex, NULL);
    pthread_cond_init(&rel->cond_tick, NULL);
}

void relogio_destruir(Relogio *rel) {
    pthread_mutex_destroy(&rel->mutex);
    pthread_cond_destroy(&rel->cond_tick);
}

void *relogio_thread(void *arg) {
    Relogio *rel = (Relogio *) arg;

    for (;;) {
        dormir_ms(rel->duracao_tick_ms);

        pthread_mutex_lock(&rel->mutex);
        if (rel->encerrar) {
            pthread_mutex_unlock(&rel->mutex);
            break;
        }
        rel->tick_atual++;
        /* Acorda TODAS as threads bloqueadas esperando o próximo tick.
         * broadcast é necessário pois múltiplos carros podem estar
         * dormindo na mesma variável de condição. */
        pthread_cond_broadcast(&rel->cond_tick);
        pthread_mutex_unlock(&rel->mutex);
    }

    return NULL;
}

long relogio_esperar_proximo_tick(Relogio *rel, long ultimo_tick_visto) {
    pthread_mutex_lock(&rel->mutex);
    while (rel->tick_atual == ultimo_tick_visto && !rel->encerrar) {
        /* pthread_cond_wait libera o mutex e bloqueia a thread SEM
         * consumir CPU, sendo acordada apenas pelo broadcast do relógio. */
        pthread_cond_wait(&rel->cond_tick, &rel->mutex);
    }
    long tick = rel->tick_atual;
    pthread_mutex_unlock(&rel->mutex);
    return tick;
}

void relogio_sinalizar_fim(Relogio *rel) {
    pthread_mutex_lock(&rel->mutex);
    rel->encerrar = 1;
    pthread_cond_broadcast(&rel->cond_tick);
    pthread_mutex_unlock(&rel->mutex);
}

int relogio_deve_encerrar(Relogio *rel) {
    int valor;
    pthread_mutex_lock(&rel->mutex);
    valor = rel->encerrar;
    pthread_mutex_unlock(&rel->mutex);
    return valor;
}

long relogio_obter_tick_atual(Relogio *rel) {
    long valor;
    pthread_mutex_lock(&rel->mutex);
    valor = rel->tick_atual;
    pthread_mutex_unlock(&rel->mutex);
    return valor;
}
