#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include "mapa.h"
#include "relogio.h"
#include "semaforos.h"
#include "veiculos.h"
#include "sincronizacao.h"

#define NUM_CARROS_COMUNS  14
#define NUM_AMBULANCIAS    1
#define NUM_VEICULOS_TOTAL (NUM_CARROS_COMUNS + NUM_AMBULANCIAS)

#define DURACAO_TICK_MS        250
#define PERIODO_TROCA_SINAL    16   /* ticks até alternância automática */
#define DURACAO_SIMULACAO_TICKS 240

static void dormir_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

typedef struct {
    Mapa *mapa;
    Relogio *relogio;
    int n_ticks_exibidos;
} ContextoVisualizacao;

/* Desenha um caractere representando o conteúdo da célula (linha,col). */
static char desenhar_celula(Mapa *mapa, ControleSemaforos *ctrl, int linha, int col) {
    Celula *c = &mapa->celulas[linha][col];

    if (c->tipo == CELULA_PAREDE) return ' ';

    /* Leitura sincronizada: c->ocupada é escrito concorrentemente pelas
     * threads de veículos (em mapa_tentar_ocupar/mapa_liberar e na
     * lógica de tentar_mover), então a visualização também precisa
     * travar o mutex da célula antes de ler, evitando data race. */
    pthread_mutex_lock(&c->mutex);
    int ocupada = c->ocupada;
    pthread_mutex_unlock(&c->mutex);

    if (ocupada) {
        return '#'; /* veiculo generico; identificacao detalhada vai na legenda */
    }

    if (c->tipo == CELULA_CRUZAMENTO) {
        int lc = linha / (TAM_TRECHO + 1);
        int cc = col   / (TAM_TRECHO + 1);
        Cruzamento *cr = semaforos_get_cruzamento(ctrl, lc, cc);
        pthread_mutex_lock(&cr->mutex);
        CorSinal ns = cr->sinal[EIXO_NS];
        pthread_mutex_unlock(&cr->mutex);
        return (ns == SINAL_VERDE) ? '+' : 'x';
    }

    /* célula de rua livre */
    if (linha % (TAM_TRECHO + 1) == 0) return '-'; /* trecho horizontal */
    if (col   % (TAM_TRECHO + 1) == 0) return '|'; /* trecho vertical   */
    return '.';
}

static void renderizar(Mapa *mapa, ControleSemaforos *ctrl, Veiculo *veiculos,
                        long tick, int n_veiculos) {
    printf("\033[H\033[J"); /* limpa terminal (ANSI) */
    printf("=== SIMULADOR DE TRAFEGO URBANO ===  tick=%ld\n\n", tick);

    for (int i = 0; i < MAPA_LINHAS; i++) {
        for (int j = 0; j < MAPA_COLS; j++) {
            putchar(desenhar_celula(mapa, ctrl, i, j));
        }
        putchar('\n');
    }

    printf("\nLegenda: '+' cruzamento (verde NS) | 'x' cruzamento (vermelho NS) | "
           "'#' veiculo | '-','|','.' vias livres\n\n");

    printf("Veiculos ativos:\n");
    for (int i = 0; i < n_veiculos; i++) {
        Veiculo *v = &veiculos[i];
        pthread_mutex_lock(&v->mutex_estado);
        if (v->estado == ESTADO_RODANDO) {
            printf("  %-5s %s pos=(%2d,%2d) dir=%d vel=%d\n",
                   v->nome,
                   v->tipo == TIPO_AMBULANCIA ? "[AMBULANCIA]" : "[carro]     ",
                   v->linha, v->col, v->direcao, v->velocidade);
        }
        pthread_mutex_unlock(&v->mutex_estado);
    }
    fflush(stdout);
}

/* Thread de visualização: redesenha a tela periodicamente, observando o
 * relógio global (evita espera ocupada usando o mesmo mecanismo de
 * tick dos veículos). */
typedef struct {
    Mapa *mapa;
    ControleSemaforos *ctrl;
    Veiculo *veiculos;
    Relogio *relogio;
    int n_veiculos;
} ArgVisualizacao;

void *thread_visualizacao(void *arg) {
    ArgVisualizacao *av = (ArgVisualizacao *) arg;
    long ultimo_tick = -1;

    while (!relogio_deve_encerrar(av->relogio)) {
        long tick = relogio_esperar_proximo_tick(av->relogio, ultimo_tick);
        ultimo_tick = tick;
        if (relogio_deve_encerrar(av->relogio)) break;
        renderizar(av->mapa, av->ctrl, av->veiculos, tick, av->n_veiculos);
    }
    return NULL;
}

/* Posições e direções de partida dos veículos, distribuídas pelas bordas
 * do mapa para que entrem na malha de formas variadas. */
static void definir_posicao_inicial(int indice, int *linha, int *col, Direcao *dir) {
    /* Distribui veículos ciclicamente pelas 4 colunas de entrada vertical
     * (topo, sentido SUL) e pelas 2 linhas de entrada horizontal de mão
     * única (oeste, sentido LESTE), garantindo cobertura do mapa. */
    int padrao = indice % 6;

    switch (padrao) {
        case 0: case 1: case 2: case 3: {
            /* entra pelo topo de uma das colunas de cruzamento, sentido SUL */
            int cc = padrao;
            int l0, c0;
            mapa_coord_cruzamento(0, cc, &l0, &c0);
            *linha = 0;
            *col = c0;
            *dir = DIR_SUL;
            break;
        }
        case 4: {
            /* entra pela esquerda na linha única 1, sentido LESTE */
            *linha = 1 * (TAM_TRECHO + 1);
            *col = 0;
            *dir = DIR_LESTE;
            break;
        }
        default: {
            /* entra pela esquerda na linha única 3, sentido LESTE */
            *linha = 3 * (TAM_TRECHO + 1);
            *col = 0;
            *dir = DIR_LESTE;
            break;
        }
    }
}

int main(void) {
    srand((unsigned int) time(NULL));

    Mapa mapa;
    Relogio relogio;
    ControleSemaforos semaforos;
    Veiculo veiculos[NUM_VEICULOS_TOTAL];

    sincronizacao_inicializar();
    mapa_inicializar(&mapa);
    relogio_inicializar(&relogio, DURACAO_TICK_MS);
    semaforos_inicializar(&semaforos, PERIODO_TROCA_SINAL);

    /* Cria os veículos comuns com velocidades variadas (rápido/médio/lento),
     * conforme exigido na seção 3.3 do enunciado. */
    Velocidade velocidades[3] = { VEL_RAPIDO, VEL_MEDIO, VEL_LENTO };
    for (int i = 0; i < NUM_CARROS_COMUNS; i++) {
        int linha_ini, col_ini;
        Direcao dir_ini;
        definir_posicao_inicial(i, &linha_ini, &col_ini, &dir_ini);

        /* Evita conflito de ocupação inicial duplicada tentando próxima
         * célula livre na mesma direção, se necessário. */
        while (!mapa_tentar_ocupar(&mapa, linha_ini, col_ini, i)) {
            int nl = linha_ini, nc = col_ini;
            if (dir_ini == DIR_SUL) nl++; else nc++;
            linha_ini = nl; col_ini = nc;
        }
        mapa_liberar(&mapa, linha_ini, col_ini); /* será ocupado de novo em veiculo_inicializar */

        veiculo_inicializar(&veiculos[i], i, TIPO_CARRO_COMUM,
                            velocidades[i % 3], linha_ini, col_ini, dir_ini,
                            &mapa, &semaforos, &relogio);
    }

    /* Cria a ambulância (id = NUM_CARROS_COMUNS). */
    {
        int idx = NUM_CARROS_COMUNS;
        int linha_ini, col_ini;
        Direcao dir_ini;
        definir_posicao_inicial(idx, &linha_ini, &col_ini, &dir_ini);
        while (!mapa_tentar_ocupar(&mapa, linha_ini, col_ini, idx)) {
            int nl = linha_ini, nc = col_ini;
            if (dir_ini == DIR_SUL) nl++; else nc++;
            linha_ini = nl; col_ini = nc;
        }
        mapa_liberar(&mapa, linha_ini, col_ini);

        veiculo_inicializar(&veiculos[idx], idx, TIPO_AMBULANCIA,
                            VEL_RAPIDO, linha_ini, col_ini, dir_ini,
                            &mapa, &semaforos, &relogio);
    }

    /* Cria threads: relógio, controlador de semáforos, veículos e visualização. */
    pthread_t tid_relogio, tid_semaforos, tid_visualizacao;
    pthread_create(&tid_relogio, NULL, relogio_thread, &relogio);
    pthread_create(&tid_semaforos, NULL, semaforos_thread_controlador, &semaforos);

    for (int i = 0; i < NUM_VEICULOS_TOTAL; i++) {
        pthread_create(&veiculos[i].thread, NULL, veiculo_thread, &veiculos[i]);
    }

    ArgVisualizacao av = { &mapa, &semaforos, veiculos, &relogio, NUM_VEICULOS_TOTAL };
    pthread_create(&tid_visualizacao, NULL, thread_visualizacao, &av);

    /* Aguarda a duração configurada da simulação (em ticks) antes de
     * sinalizar o encerramento ordenado de todas as threads. */
    while (relogio_obter_tick_atual(&relogio) < DURACAO_SIMULACAO_TICKS) {
        dormir_ms(50);
    }

    relogio_sinalizar_fim(&relogio);
    g_semaforos_encerrar = 1;
    /* Acorda qualquer thread de veículo presa em semaforo_esperar_verde
     * (esperando sinal verde) ou em semaforo_entrar_cruzamento, evitando
     * que pthread_join trave indefinidamente. */
    semaforos_broadcast_encerramento(&semaforos);

    pthread_join(tid_relogio, NULL);
    pthread_join(tid_semaforos, NULL);
    pthread_join(tid_visualizacao, NULL);
    for (int i = 0; i < NUM_VEICULOS_TOTAL; i++) {
        pthread_join(veiculos[i].thread, NULL);
        veiculo_destruir(&veiculos[i]);
    }

    printf("\n=== Simulacao finalizada apos %ld ticks ===\n", relogio_obter_tick_atual(&relogio));

    mapa_destruir(&mapa);
    relogio_destruir(&relogio);
    semaforos_destruir(&semaforos);
    sincronizacao_destruir();

    return 0;
}
