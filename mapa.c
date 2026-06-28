#include "mapa.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Layout lógico (replica Mapa.txt):
 *
 *   Linha de cruzamento 0 -> via horizontal DUPLA  (---+---+---+---+---)
 *   Linha de cruzamento 1 -> via horizontal ÚNICA  (>>>+>>>+>>>+>>>+>>>) sentido LESTE
 *   Linha de cruzamento 2 -> via horizontal DUPLA
 *   Linha de cruzamento 3 -> via horizontal ÚNICA  sentido LESTE
 *
 *   Todas as colunas de cruzamento -> via vertical DUPLA (sentido N<->S)
 *
 * Cada par de cruzamentos consecutivos (na mesma linha ou coluna) é ligado
 * por um trecho de TAM_TRECHO células de rua.
 */

/* (Tipo de via horizontal é determinado em semaforos.c, via
 * semaforos_tipo_via_horizontal, para uso na lógica de movimento dos
 * veículos.) */

void mapa_coord_cruzamento(int linha_cruz, int col_cruz, int *linha, int *col) {
    *linha = linha_cruz * (TAM_TRECHO + 1);
    *col   = col_cruz   * (TAM_TRECHO + 1);
}

int mapa_e_cruzamento(int linha, int col) {
    if (linha % (TAM_TRECHO + 1) != 0) return 0;
    if (col   % (TAM_TRECHO + 1) != 0) return 0;
    int linha_cruz = linha / (TAM_TRECHO + 1);
    int col_cruz   = col   / (TAM_TRECHO + 1);
    return (linha_cruz >= 0 && linha_cruz < MAPA_LINHAS_CRUZ &&
            col_cruz   >= 0 && col_cruz   < MAPA_COLS_CRUZ);
}

static void celula_init(Celula *c, TipoCelula tipo) {
    c->tipo = tipo;
    c->ocupada = 0;
    c->ocupante_id = -1;
    pthread_mutex_init(&c->mutex, NULL);
}

void mapa_inicializar(Mapa *mapa) {
    /* 1) Começa tudo como PAREDE (área não navegável) */
    for (int i = 0; i < MAPA_LINHAS; i++) {
        for (int j = 0; j < MAPA_COLS; j++) {
            celula_init(&mapa->celulas[i][j], CELULA_PAREDE);
        }
    }

    /* 2) Marca os cruzamentos */
    for (int lc = 0; lc < MAPA_LINHAS_CRUZ; lc++) {
        for (int cc = 0; cc < MAPA_COLS_CRUZ; cc++) {
            int linha, col;
            mapa_coord_cruzamento(lc, cc, &linha, &col);
            mapa->celulas[linha][col].tipo = CELULA_CRUZAMENTO;
        }
    }

    /* 3) Trechos horizontais entre cruzamentos da mesma linha */
    for (int lc = 0; lc < MAPA_LINHAS_CRUZ; lc++) {
        int linha, col_a;
        mapa_coord_cruzamento(lc, 0, &linha, &col_a);
        for (int cc = 0; cc < MAPA_COLS_CRUZ - 1; cc++) {
            int l, cini, cfim;
            mapa_coord_cruzamento(lc, cc, &l, &cini);
            mapa_coord_cruzamento(lc, cc + 1, &l, &cfim);
            for (int c = cini + 1; c < cfim; c++) {
                mapa->celulas[l][c].tipo = CELULA_RUA;
            }
        }
        /* trechos de extremidade (antes do primeiro e após o último cruzamento) */
        int l0, c0;
        mapa_coord_cruzamento(lc, 0, &l0, &c0);
        for (int c = c0 - TAM_TRECHO; c < c0; c++) {
            if (c >= 0) mapa->celulas[l0][c].tipo = CELULA_RUA;
        }
        int lN, cN;
        mapa_coord_cruzamento(lc, MAPA_COLS_CRUZ - 1, &lN, &cN);
        for (int c = cN + 1; c <= cN + TAM_TRECHO; c++) {
            if (c < MAPA_COLS) mapa->celulas[lN][c].tipo = CELULA_RUA;
        }
    }

    /* 4) Trechos verticais entre cruzamentos da mesma coluna */
    for (int cc = 0; cc < MAPA_COLS_CRUZ; cc++) {
        for (int lc = 0; lc < MAPA_LINHAS_CRUZ - 1; lc++) {
            int lini, c, lfim, c2;
            mapa_coord_cruzamento(lc, cc, &lini, &c);
            mapa_coord_cruzamento(lc + 1, cc, &lfim, &c2);
            for (int l = lini + 1; l < lfim; l++) {
                mapa->celulas[l][c].tipo = CELULA_RUA;
            }
        }
        /* extremidades verticais (entrada pelo topo / saída pela base) */
        int l0, c0;
        mapa_coord_cruzamento(0, cc, &l0, &c0);
        for (int l = l0 - TAM_TRECHO; l < l0; l++) {
            if (l >= 0) mapa->celulas[l][c0].tipo = CELULA_RUA;
        }
        int lN, cN;
        mapa_coord_cruzamento(MAPA_LINHAS_CRUZ - 1, cc, &lN, &cN);
        for (int l = lN + 1; l <= lN + TAM_TRECHO; l++) {
            if (l < MAPA_LINHAS) mapa->celulas[l][cN].tipo = CELULA_RUA;
        }
    }
}

void mapa_destruir(Mapa *mapa) {
    for (int i = 0; i < MAPA_LINHAS; i++) {
        for (int j = 0; j < MAPA_COLS; j++) {
            pthread_mutex_destroy(&mapa->celulas[i][j].mutex);
        }
    }
}

int mapa_celula_valida(Mapa *mapa, int linha, int col) {
    if (linha < 0 || linha >= MAPA_LINHAS || col < 0 || col >= MAPA_COLS) {
        return 0;
    }
    TipoCelula t = mapa->celulas[linha][col].tipo;
    return (t == CELULA_RUA || t == CELULA_CRUZAMENTO);
}

int mapa_tentar_ocupar(Mapa *mapa, int linha, int col, int veiculo_id) {
    if (!mapa_celula_valida(mapa, linha, col)) return 0;

    Celula *c = &mapa->celulas[linha][col];
    int sucesso = 0;

    pthread_mutex_lock(&c->mutex);
    if (!c->ocupada) {
        c->ocupada = 1;
        c->ocupante_id = veiculo_id;
        sucesso = 1;
    }
    pthread_mutex_unlock(&c->mutex);

    return sucesso;
}

void mapa_liberar(Mapa *mapa, int linha, int col) {
    if (linha < 0 || linha >= MAPA_LINHAS || col < 0 || col >= MAPA_COLS) return;
    Celula *c = &mapa->celulas[linha][col];

    pthread_mutex_lock(&c->mutex);
    c->ocupada = 0;
    c->ocupante_id = -1;
    pthread_mutex_unlock(&c->mutex);
}
