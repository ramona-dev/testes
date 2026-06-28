#ifndef VEICULOS_H
#define VEICULOS_H

#include <pthread.h>
#include "mapa.h"
#include "semaforos.h"
#include "relogio.h"

#define MAX_VEICULOS      20
#define MAX_ROTA          64
#define NOME_TAM          16

typedef enum {
    DIR_NORTE = 0,  /* anda diminuindo a linha (para cima)   */
    DIR_SUL,        /* anda aumentando a linha (para baixo)  */
    DIR_LESTE,      /* anda aumentando a coluna (direita)    */
    DIR_OESTE       /* anda diminuindo a coluna (esquerda)   */
} Direcao;

typedef enum {
    VEL_RAPIDO = 1,  /* move a cada 1 tick */
    VEL_MEDIO  = 2,  /* move a cada 2 ticks */
    VEL_LENTO  = 4   /* move a cada 4 ticks */
} Velocidade;

typedef enum {
    TIPO_CARRO_COMUM = 0,
    TIPO_AMBULANCIA  = 1
} TipoVeiculo;

typedef enum {
    ESTADO_RODANDO = 0,
    ESTADO_FINALIZADO = 1
} EstadoVeiculo;

/* Cada veículo é controlado por uma thread independente. */
typedef struct {
    int id;
    char nome[NOME_TAM];
    TipoVeiculo tipo;
    Velocidade velocidade;
    Direcao direcao;

    int linha, col;          /* posição atual na matriz do mapa */

    /* Rota: sequência de direções que o veículo deve seguir até saída.
     * Mantemos simples: o veículo segue em frente até um cruzamento e
     * então escolhe (de forma pseudo-aleatória, respeitando mão da via)
     * a próxima direção válida, até atingir o limite de movimentos. */
    int passos_restantes;

    long ultimo_tick_movimento;
    EstadoVeiculo estado;

    pthread_t thread;

    /* Referências compartilhadas (somente leitura de ponteiro) */
    Mapa *mapa;
    ControleSemaforos *semaforos;
    Relogio *relogio;

    /* Mutex que protege o log/estado de exibição deste veículo, usado
     * pela thread de visualização. */
    pthread_mutex_t mutex_estado;
} Veiculo;

/* Inicializa os campos de um veículo (não cria a thread ainda). */
void veiculo_inicializar(Veiculo *v, int id, TipoVeiculo tipo, Velocidade vel,
                          int linha_ini, int col_ini, Direcao dir_ini,
                          Mapa *mapa, ControleSemaforos *sem, Relogio *rel);

/* Função executada pela thread do veículo. */
void *veiculo_thread(void *arg);

/* Libera mutex do veículo. */
void veiculo_destruir(Veiculo *v);

#endif /* VEICULOS_H */
