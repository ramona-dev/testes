#ifndef MAPA_H
#define MAPA_H

#include <pthread.h>

/*
 * Representação do mapa urbano.
 *
 * O mapa é uma malha (grid) de RUAS x COLUNAS células.
 * Linhas e colunas pares representam CRUZAMENTOS (semáforo).
 * Linhas e colunas ímpares representam trechos de via (corredores).
 *
 * Mapa.txt (referência do projeto):
 *
 *    v   v   v   v
 *    |   |   |   |
 *  --+---+---+---+--   <- via dupla horizontal (linha 0 de cruzamentos)
 *    |   |   |   |
 *  >>+>>>+>>>+>>>+>>   <- via de mão única -> (linha 1 de cruzamentos)
 *    |   |   |   |
 *  --+---+---+---+--   <- via dupla horizontal (linha 2 de cruzamentos)
 *    |   |   |   |
 *  >>+>>>+>>>+>>>+>>   <- via de mão única -> (linha 3 de cruzamentos)
 *    |   |   |   |
 *    ^   ^   ^   ^
 *
 * Isso gera uma malha de 4 linhas x 4 colunas de cruzamentos = 16 cruzamentos
 * (acima do mínimo de 8 exigido), com:
 *   - vias verticais de mão dupla (sentido cima<->baixo nas colunas)
 *   - 2 ruas horizontais de mão dupla (linhas de cruzamento 0 e 2)
 *   - 2 ruas horizontais de mão única, sentido ->  (linhas de cruzamento 1 e 3)
 */

#define MAPA_LINHAS_CRUZ   4   /* quantidade de linhas de cruzamentos   */
#define MAPA_COLS_CRUZ     4   /* quantidade de colunas de cruzamentos  */

/* Tamanho do trecho de via (em células) entre dois cruzamentos consecutivos */
#define TAM_TRECHO         3

/* Dimensões totais da matriz de células do mapa.
 * Cada cruzamento ocupa 1 célula; cada trecho entre cruzamentos ocupa
 * TAM_TRECHO células.
 */
#define MAPA_LINHAS  (MAPA_LINHAS_CRUZ * (TAM_TRECHO + 1) + 1)
#define MAPA_COLS    (MAPA_COLS_CRUZ   * (TAM_TRECHO + 1) + 1)

typedef enum {
    VIA_DUPLA_HORIZONTAL,
    VIA_UNICA_HORIZONTAL,  /* mão única, sentido -> (leste) */
    VIA_DUPLA_VERTICAL
} TipoVia;

typedef enum {
    CELULA_VAZIA = 0,
    CELULA_RUA,
    CELULA_CRUZAMENTO,
    CELULA_PAREDE       /* fora da malha viária, intransitável */
} TipoCelula;

/* Cada célula da malha viária. Protegida individualmente por mutex para
 * permitir exclusão mútua na ocupação (impenetrabilidade). */
typedef struct {
    TipoCelula tipo;
    int ocupada;          /* 0 = livre, 1 = ocupada */
    int ocupante_id;       /* id do veículo que ocupa, -1 se livre */
    pthread_mutex_t mutex; /* protege ocupada/ocupante_id */
} Celula;

/* Estrutura principal do mapa */
typedef struct {
    Celula celulas[MAPA_LINHAS][MAPA_COLS];
} Mapa;

/* Cria e inicializa o mapa global, definindo ruas, cruzamentos e paredes. */
void mapa_inicializar(Mapa *mapa);

/* Libera recursos (mutexes) do mapa. */
void mapa_destruir(Mapa *mapa);

/* Retorna 1 se a célula (linha,col) é uma posição válida e transitável. */
int mapa_celula_valida(Mapa *mapa, int linha, int col);

/* Retorna 1 se (linha,col) corresponde a um cruzamento. */
int mapa_e_cruzamento(int linha, int col);

/* Converte índice de cruzamento (linha_cruz, col_cruz) em coordenadas de célula. */
void mapa_coord_cruzamento(int linha_cruz, int col_cruz, int *linha, int *col);

/* Tenta ocupar a célula destino de forma atômica. Retorna 1 em sucesso. */
int mapa_tentar_ocupar(Mapa *mapa, int linha, int col, int veiculo_id);

/* Libera a célula de origem (deve ser chamado pelo veículo que a ocupava). */
void mapa_liberar(Mapa *mapa, int linha, int col);

#endif /* MAPA_H */
