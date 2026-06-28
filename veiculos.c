#include "veiculos.h"
#include "sincronizacao.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Calcula a próxima posição (linha,col) dado a direção atual. */
static void proxima_posicao(int linha, int col, Direcao dir, int *nl, int *nc) {
    *nl = linha;
    *nc = col;
    switch (dir) {
        case DIR_NORTE: *nl = linha - 1; break;
        case DIR_SUL:   *nl = linha + 1; break;
        case DIR_LESTE: *nc = col + 1;   break;
        case DIR_OESTE: *nc = col - 1;   break;
    }
}

/* Identifica o eixo de movimento (NS ou LO) de uma direção. */
static EixoVia eixo_da_direcao(Direcao dir) {
    return (dir == DIR_NORTE || dir == DIR_SUL) ? EIXO_NS : EIXO_LO;
}

/*
 * Quando o veículo está prestes a entrar em uma linha de cruzamento que é
 * via única (mão única, sentido Leste), direções OESTE não são permitidas.
 * Esta verificação garante que carros não tentem ultrapassar/violar o
 * sentido de uma via de mão única.
 */
static int direcao_permitida_na_linha(int linha, Direcao dir) {
    if (linha % (TAM_TRECHO + 1) != 0) return 1; /* não é linha de cruzamento, sem restrição extra aqui */
    int linha_cruz = linha / (TAM_TRECHO + 1);
    if (linha_cruz < 0 || linha_cruz >= MAPA_LINHAS_CRUZ) return 1;
    TipoVia tipo = semaforos_tipo_via_horizontal(linha_cruz);
    if (tipo == VIA_UNICA_HORIZONTAL && dir == DIR_OESTE) {
        return 0; /* mão única é sentido Leste; proibe Oeste */
    }
    return 1;
}

/*
 * Escolhe uma direção válida para o veículo, dado sua posição atual.
 * Tenta manter a direção atual (seguir em frente); se não for possível
 * (limite do mapa ou violação de mão única), escolhe alternativa válida
 * de forma determinística (varre N,S,L,O).
 */
static Direcao escolher_direcao(Veiculo *v) {
    Direcao candidatas[4];
    candidatas[0] = v->direcao;

    Direcao todas[4] = { DIR_NORTE, DIR_SUL, DIR_LESTE, DIR_OESTE };
    int idx = 1;
    for (int i = 0; i < 4; i++) {
        if (todas[i] != v->direcao) candidatas[idx++] = todas[i];
    }

    for (int i = 0; i < 4; i++) {
        Direcao d = candidatas[i];
        int nl, nc;
        proxima_posicao(v->linha, v->col, d, &nl, &nc);
        if (!mapa_celula_valida(v->mapa, nl, nc)) continue;
        if (!direcao_permitida_na_linha(v->linha, d)) continue;

        /* Em vias verticais não há restrição de mão única (dupla); em
         * horizontais já tratamos acima. Também evita reversão imediata
         * de 180 graus em corredor estreito para reduzir comportamento
         * artificial (mas permite em cruzamento, já tratado por exclusão
         * das candidatas se aplicável). */
        return d;
    }

    /* Nenhuma direção válida encontrada: mantém a atual (vai aguardar). */
    return v->direcao;
}

void veiculo_inicializar(Veiculo *v, int id, TipoVeiculo tipo, Velocidade vel,
                          int linha_ini, int col_ini, Direcao dir_ini,
                          Mapa *mapa, ControleSemaforos *sem, Relogio *rel) {
    v->id = id;
    snprintf(v->nome, NOME_TAM, "%s%02d", tipo == TIPO_AMBULANCIA ? "AMB" : "C", id);
    v->tipo = tipo;
    v->velocidade = vel;
    v->direcao = dir_ini;
    v->linha = linha_ini;
    v->col = col_ini;
    v->passos_restantes = 200; /* limite de movimentos antes de "sair do mapa" */
    v->ultimo_tick_movimento = -1;
    v->estado = ESTADO_RODANDO;
    v->mapa = mapa;
    v->semaforos = sem;
    v->relogio = rel;
    pthread_mutex_init(&v->mutex_estado, NULL);

    /* Marca a célula inicial como ocupada por este veículo. */
    mapa_tentar_ocupar(mapa, linha_ini, col_ini, id);
}

void veiculo_destruir(Veiculo *v) {
    pthread_mutex_destroy(&v->mutex_estado);
}

/*
 * Move o veículo uma célula adiante, se possível, respeitando:
 *  - impenetrabilidade (mapa_tentar_ocupar é atômico por célula)
 *  - ausência de teletransporte (sempre move para célula ADJACENTE)
 *  - sinais de trânsito quando a célula adjacente é um cruzamento ou
 *    quando o veículo está saindo de um cruzamento na direção de uma
 *    via controlada
 *  - estratégia de prevenção de deadlock (ordem fixa de aquisição) ao
 *    tratar origem/destino quando ambos exigem coordenação
 */
static void tentar_mover(Veiculo *v) {
    Direcao dir = escolher_direcao(v);
    int nl, nc;
    proxima_posicao(v->linha, v->col, dir, &nl, &nc);

    if (!mapa_celula_valida(v->mapa, nl, nc)) {
        /* Veículo chegou ao limite da malha: encerra sua jornada. */
        v->estado = ESTADO_FINALIZADO;
        return;
    }

    /* Se a célula de destino é um cruzamento, deve respeitar o semáforo
     * e a capacidade do cruzamento ANTES de tentar ocupar a célula. */
    if (mapa_e_cruzamento(nl, nc)) {
        int linha_cruz = nl / (TAM_TRECHO + 1);
        int col_cruz   = nc / (TAM_TRECHO + 1);
        Cruzamento *cr = semaforos_get_cruzamento(v->semaforos, linha_cruz, col_cruz);

        if (v->tipo == TIPO_AMBULANCIA) {
            /* Ambulância solicita prioridade antes de esperar normalmente. */
            EixoVia eixo = eixo_da_direcao(dir);
            pthread_mutex_lock(&cr->mutex);
            int ja_verde = (cr->sinal[eixo] == SINAL_VERDE);
            pthread_mutex_unlock(&cr->mutex);
            if (!ja_verde) {
                semaforo_solicitar_prioridade_ambulancia(cr, eixo);
            }
        }

        /* Bloqueia (sem busy-wait) até o sinal do eixo de movimento
         * ficar verde, ou até a simulação ser encerrada. */
        int sinal_verde = semaforo_esperar_verde(cr, eixo_da_direcao(dir));
        if (!sinal_verde) {
            /* Simulação está sendo encerrada: aborta o movimento sem
             * tentar ocupar o cruzamento, permitindo que a thread
             * termine de forma limpa. */
            v->estado = ESTADO_FINALIZADO;
            return;
        }

        /* Garante exclusão por capacidade do cruzamento (no máximo
         * CAPACIDADE_CRUZAMENTO veículos dentro dele simultaneamente),
         * prevenindo colisões mesmo com sinal verde. */
        int entrou = semaforo_entrar_cruzamento(cr);
        if (!entrou) {
            /* Também encerrando: aborta sem ocupar a célula do cruzamento. */
            v->estado = ESTADO_FINALIZADO;
            return;
        }

        /* Estratégia de prevenção de deadlock: ordem fixa de aquisição
         * dos mutexes de célula origem/destino (ver sincronizacao.c). */
        int id_origem = v->linha * MAPA_COLS + v->col;
        int id_destino = nl * MAPA_COLS + nc;
        Celula *c_origem = &v->mapa->celulas[v->linha][v->col];
        Celula *c_destino = &v->mapa->celulas[nl][nc];

        travar_em_ordem(&c_origem->mutex, id_origem, &c_destino->mutex, id_destino);
        int pode_entrar = !c_destino->ocupada;
        if (pode_entrar) {
            c_destino->ocupada = 1;
            c_destino->ocupante_id = v->id;
            c_origem->ocupada = 0;
            c_origem->ocupante_id = -1;
        }
        destravar_em_ordem(&c_origem->mutex, id_origem, &c_destino->mutex, id_destino);

        semaforo_sair_cruzamento(cr);

        if (pode_entrar) {
            v->linha = nl;
            v->col = nc;
            v->direcao = dir;
            v->passos_restantes--;
        }
        return;
    }

    /* Movimento comum entre células de rua (fora de cruzamento): aplica
     * a mesma ordem fixa de aquisição para manter consistência e evitar
     * deadlock também nesses trechos (relevante em vias de mão dupla,
     * onde dois carros podem competir por células vizinhas em sentidos
     * opostos). */
    int id_origem = v->linha * MAPA_COLS + v->col;
    int id_destino = nl * MAPA_COLS + nc;
    Celula *c_origem = &v->mapa->celulas[v->linha][v->col];
    Celula *c_destino = &v->mapa->celulas[nl][nc];

    travar_em_ordem(&c_origem->mutex, id_origem, &c_destino->mutex, id_destino);
    int pode_entrar = !c_destino->ocupada;
    if (pode_entrar) {
        c_destino->ocupada = 1;
        c_destino->ocupante_id = v->id;
        c_origem->ocupada = 0;
        c_origem->ocupante_id = -1;
    }
    destravar_em_ordem(&c_origem->mutex, id_origem, &c_destino->mutex, id_destino);

    if (pode_entrar) {
        v->linha = nl;
        v->col = nc;
        v->direcao = dir;
        v->passos_restantes--;
    }
    /* Se não conseguiu entrar (célula ocupada por outro veículo), o
     * carro simplesmente aguarda o próximo tick e tenta novamente -
     * não há espera ocupada aqui porque quem bloqueia a thread entre
     * tentativas é relogio_esperar_proximo_tick() no laço principal. */
}

void *veiculo_thread(void *arg) {
    Veiculo *v = (Veiculo *) arg;
    long ultimo_tick = -1;

    while (!relogio_deve_encerrar(v->relogio) && v->passos_restantes > 0) {
        /* Bloqueia (sem espera ocupada) até o próximo tick global. */
        long tick = relogio_esperar_proximo_tick(v->relogio, ultimo_tick);
        ultimo_tick = tick;

        if (relogio_deve_encerrar(v->relogio)) break;

        /* Respeita a velocidade do veículo: só tenta mover em ticks
         * múltiplos do seu intervalo de velocidade. */
        if (tick % (long) v->velocidade != 0) continue;

        pthread_mutex_lock(&v->mutex_estado);
        tentar_mover(v);
        EstadoVeiculo estado_atual = v->estado;
        pthread_mutex_unlock(&v->mutex_estado);

        if (estado_atual == ESTADO_FINALIZADO) break;
    }

    pthread_mutex_lock(&v->mutex_estado);
    if (v->estado != ESTADO_FINALIZADO) {
        v->estado = ESTADO_FINALIZADO;
        mapa_liberar(v->mapa, v->linha, v->col);
    }
    pthread_mutex_unlock(&v->mutex_estado);

    log_evento("[INFO] Veiculo %s finalizou sua rota.\n", v->nome);
    return NULL;
}
