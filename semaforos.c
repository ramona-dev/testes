#define _POSIX_C_SOURCE 200809L
#include "semaforos.h"
#include "sincronizacao.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void dormir_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

atomic_int g_semaforos_encerrar = 0;

TipoVia semaforos_tipo_via_horizontal(int linha_cruz) {
    return (linha_cruz % 2 == 0) ? VIA_DUPLA_HORIZONTAL : VIA_UNICA_HORIZONTAL;
}

void semaforos_inicializar(ControleSemaforos *ctrl, int periodo_ticks) {
    ctrl->periodo_ticks = periodo_ticks;

    for (int lc = 0; lc < MAPA_LINHAS_CRUZ; lc++) {
        for (int cc = 0; cc < MAPA_COLS_CRUZ; cc++) {
            Cruzamento *cr = &ctrl->cruzamentos[lc][cc];
            cr->linha_cruz = lc;
            cr->col_cruz = cc;

            /* Estado inicial: alterna por linha de cruzamento para que nem
             * todos comecem iguais (mais realista visualmente). */
            if (lc % 2 == 0) {
                cr->sinal[EIXO_LO] = SINAL_VERDE;
                cr->sinal[EIXO_NS] = SINAL_VERMELHO;
            } else {
                cr->sinal[EIXO_LO] = SINAL_VERMELHO;
                cr->sinal[EIXO_NS] = SINAL_VERDE;
            }

            cr->ambulancia_solicitando = 0;
            cr->eixo_solicitado_ambulancia = -1;

            pthread_mutex_init(&cr->mutex, NULL);
            pthread_cond_init(&cr->cond_sinal, NULL);

            cr->sem_capacidade = malloc(sizeof(sem_t));
            sem_init(cr->sem_capacidade, 0, CAPACIDADE_CRUZAMENTO);
        }
    }
}

void semaforos_destruir(ControleSemaforos *ctrl) {
    for (int lc = 0; lc < MAPA_LINHAS_CRUZ; lc++) {
        for (int cc = 0; cc < MAPA_COLS_CRUZ; cc++) {
            Cruzamento *cr = &ctrl->cruzamentos[lc][cc];
            pthread_mutex_destroy(&cr->mutex);
            pthread_cond_destroy(&cr->cond_sinal);
            sem_destroy(cr->sem_capacidade);
            free(cr->sem_capacidade);
        }
    }
}

Cruzamento *semaforos_get_cruzamento(ControleSemaforos *ctrl, int linha_cruz, int col_cruz) {
    if (linha_cruz < 0 || linha_cruz >= MAPA_LINHAS_CRUZ ||
        col_cruz   < 0 || col_cruz   >= MAPA_COLS_CRUZ) {
        return NULL;
    }
    return &ctrl->cruzamentos[linha_cruz][col_cruz];
}

/*
 * Troca o sinal de um cruzamento de forma segura: o eixo que vai abrir só
 * é liberado depois que o eixo oposto foi colocado em VERMELHO e o mutex
 * garante que nenhum carro observe um estado intermediário/inconsistente
 * (ex.: os dois eixos verdes ao mesmo tempo).
 */
static void trocar_sinal(Cruzamento *cr) {
    pthread_mutex_lock(&cr->mutex);

    if (cr->sinal[EIXO_LO] == SINAL_VERDE) {
        cr->sinal[EIXO_LO] = SINAL_VERMELHO;
        cr->sinal[EIXO_NS] = SINAL_VERDE;
    } else {
        cr->sinal[EIXO_NS] = SINAL_VERMELHO;
        cr->sinal[EIXO_LO] = SINAL_VERDE;
    }

    /* Acorda todas as threads de veículos bloqueadas esperando qualquer
     * um dos dois eixos deste cruzamento. */
    pthread_cond_broadcast(&cr->cond_sinal);
    pthread_mutex_unlock(&cr->mutex);
}

/*
 * Força o eixo solicitado pela ambulância a ficar verde imediatamente,
 * de forma segura (sob o mesmo mutex), e mantém o eixo oposto vermelho.
 */
static void forcar_verde_para_ambulancia(Cruzamento *cr, EixoVia eixo) {
    pthread_mutex_lock(&cr->mutex);

    EixoVia oposto = (eixo == EIXO_NS) ? EIXO_LO : EIXO_NS;
    cr->sinal[oposto] = SINAL_VERMELHO;
    cr->sinal[eixo] = SINAL_VERDE;

    pthread_cond_broadcast(&cr->cond_sinal);
    pthread_mutex_unlock(&cr->mutex);
}

void *semaforos_thread_controlador(void *arg) {
    ControleSemaforos *ctrl = (ControleSemaforos *) arg;
    int contador_ticks = 0;

    while (!g_semaforos_encerrar) {
        dormir_ms(100); /* checa a cada 100ms (independente do tick dos carros) */
        contador_ticks++;

        for (int lc = 0; lc < MAPA_LINHAS_CRUZ; lc++) {
            for (int cc = 0; cc < MAPA_COLS_CRUZ; cc++) {
                Cruzamento *cr = &ctrl->cruzamentos[lc][cc];

                pthread_mutex_lock(&cr->mutex);
                int tem_ambulancia = cr->ambulancia_solicitando;
                int eixo_amb = cr->eixo_solicitado_ambulancia;
                pthread_mutex_unlock(&cr->mutex);

                if (tem_ambulancia) {
                    /* Prioridade da ambulância: abre o eixo necessário
                     * assim que possível, sem violar exclusão mútua das
                     * células (isso é garantido pelo semáforo de
                     * capacidade do cruzamento e pelos mutexes de célula,
                     * não pelo sinal em si). */
                    forcar_verde_para_ambulancia(cr, (EixoVia) eixo_amb);
                    log_evento("[AMBULANCIA] Prioridade concedida no cruzamento (%d,%d) eixo=%s\n",
                               lc, cc, eixo_amb == EIXO_NS ? "NS" : "LO");

                    pthread_mutex_lock(&cr->mutex);
                    cr->ambulancia_solicitando = 0;
                    cr->eixo_solicitado_ambulancia = -1;
                    pthread_mutex_unlock(&cr->mutex);
                }
            }
        }

        if (contador_ticks >= ctrl->periodo_ticks) {
            contador_ticks = 0;
            for (int lc = 0; lc < MAPA_LINHAS_CRUZ; lc++) {
                for (int cc = 0; cc < MAPA_COLS_CRUZ; cc++) {
                    trocar_sinal(&ctrl->cruzamentos[lc][cc]);
                }
            }
        }
    }

    return NULL;
}

int semaforo_esperar_verde(Cruzamento *cr, EixoVia eixo) {
    pthread_mutex_lock(&cr->mutex);
    while (cr->sinal[eixo] != SINAL_VERDE && !g_semaforos_encerrar) {
        /* Bloqueia sem consumir CPU; só é acordada quando o sinal muda
         * (trocar_sinal / forcar_verde_para_ambulancia fazem o broadcast)
         * ou quando a simulação está sendo encerrada (ver
         * semaforos_thread_controlador, que faz um broadcast final). */
        pthread_cond_wait(&cr->cond_sinal, &cr->mutex);
    }
    int ficou_verde = (cr->sinal[eixo] == SINAL_VERDE);
    pthread_mutex_unlock(&cr->mutex);
    return ficou_verde;
}

void semaforo_solicitar_prioridade_ambulancia(Cruzamento *cr, EixoVia eixo) {
    pthread_mutex_lock(&cr->mutex);
    cr->ambulancia_solicitando = 1;
    cr->eixo_solicitado_ambulancia = eixo;
    pthread_mutex_unlock(&cr->mutex);

    log_evento("[AMBULANCIA] Solicitando prioridade no cruzamento (%d,%d) eixo=%s\n",
               cr->linha_cruz, cr->col_cruz, eixo == EIXO_NS ? "NS" : "LO");
}

/* Acorda explicitamente todas as threads que possam estar bloqueadas em
 * semaforo_esperar_verde de qualquer cruzamento. Chamado no encerramento
 * da simulação para garantir que nenhuma thread de veículo fique
 * presa indefinidamente em pthread_cond_wait. */
void semaforos_broadcast_encerramento(ControleSemaforos *ctrl) {
    for (int lc = 0; lc < MAPA_LINHAS_CRUZ; lc++) {
        for (int cc = 0; cc < MAPA_COLS_CRUZ; cc++) {
            Cruzamento *cr = &ctrl->cruzamentos[lc][cc];
            pthread_mutex_lock(&cr->mutex);
            pthread_cond_broadcast(&cr->cond_sinal);
            pthread_mutex_unlock(&cr->mutex);
        }
    }
}

int semaforo_entrar_cruzamento(Cruzamento *cr) {
    /*
     * sem_wait bloqueia sem busy-waiting até haver vaga de capacidade.
     * Usamos sem_timedwait em loop curto apenas para conseguir observar
     * g_semaforos_encerrar periodicamente sem busy-wait real (o tempo
     * entre tentativas é gasto bloqueado no kernel, não girando em CPU).
     * Retorna 1 se conseguiu entrar, 0 se desistiu por encerramento.
     */
    while (!g_semaforos_encerrar) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200 * 1000000L; /* 200ms */
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        if (sem_timedwait(cr->sem_capacidade, &ts) == 0) {
            return 1;
        }
        /* timeout: volta a checar g_semaforos_encerrar e tenta de novo */
    }
    return 0;
}

void semaforo_sair_cruzamento(Cruzamento *cr) {
    sem_post(cr->sem_capacidade);
}
