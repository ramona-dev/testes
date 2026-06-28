#include "sincronizacao.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

pthread_mutex_t g_mutex_log;

void sincronizacao_inicializar(void) {
    if (pthread_mutex_init(&g_mutex_log, NULL) != 0) {
        fprintf(stderr, "Erro ao inicializar mutex de log\n");
        exit(EXIT_FAILURE);
    }
}

void sincronizacao_destruir(void) {
    pthread_mutex_destroy(&g_mutex_log);
}

void log_evento(const char *formato, ...) {
    va_list args;
    pthread_mutex_lock(&g_mutex_log);

    va_start(args, formato);
    vfprintf(stdout, formato, args);
    va_end(args);
    fflush(stdout);

    pthread_mutex_unlock(&g_mutex_log);
}

/*
 * Estratégia de prevenção de deadlock: ORDEM FIXA DE AQUISIÇÃO DE RECURSOS.
 *
 * Quando um veículo precisa travar dois mutexes simultaneamente (célula de
 * origem e célula de destino), ele NUNCA trava na ordem "origem depois
 * destino" de forma incondicional. Em vez disso, ambos os mutexes são
 * identificados por um índice global único (id = linha*MAPA_COLS+col) e
 * são sempre adquiridos na ordem crescente desse índice.
 *
 * Por que isso evita deadlock:
 * Suponha os veículos V1 (de A para B) e V2 (de B para A) tentando se
 * mover ao mesmo tempo, exigindo os mutexes de A e B. Sem ordem fixa,
 * V1 poderia travar A e esperar por B, enquanto V2 trava B e espera por A:
 * espera circular -> deadlock. Com ordem fixa (ex.: sempre o menor índice
 * primeiro), tanto V1 quanto V2 tentarão travar o MESMO mutex primeiro
 * (o de menor índice). Um deles conseguirá e o outro bloqueará na
 * primeira aquisição, nunca chegando a travar o segundo recurso enquanto
 * espera o primeiro. Isso elimina a espera circular.
 */
void travar_em_ordem(pthread_mutex_t *mutexA, int idA,
                      pthread_mutex_t *mutexB, int idB) {
    if (idA == idB) {
        /* mesmo recurso: trava uma única vez */
        pthread_mutex_lock(mutexA);
        return;
    }
    if (idA < idB) {
        pthread_mutex_lock(mutexA);
        pthread_mutex_lock(mutexB);
    } else {
        pthread_mutex_lock(mutexB);
        pthread_mutex_lock(mutexA);
    }
}

void destravar_em_ordem(pthread_mutex_t *mutexA, int idA,
                         pthread_mutex_t *mutexB, int idB) {
    if (idA == idB) {
        pthread_mutex_unlock(mutexA);
        return;
    }
    /* ordem de destravamento não é crítica para deadlock, mas mantemos
     * o inverso da ordem de aquisição por boa prática. */
    if (idA < idB) {
        pthread_mutex_unlock(mutexB);
        pthread_mutex_unlock(mutexA);
    } else {
        pthread_mutex_unlock(mutexA);
        pthread_mutex_unlock(mutexB);
    }
}
