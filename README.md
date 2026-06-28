# Traffic-simulator

Simulador concorrente de tráfego urbano em C, desenvolvido para a disciplina
de Sistemas Operacionais (tema: Concorrência, Sincronização e Deadlocks).

Cada veículo é representado por uma thread (Pthreads) que compete por
espaços de uma malha viária com 16 cruzamentos, semáforos, vias de mão
única e mão dupla, e uma ambulância com prioridade de passagem.

## Requisitos

- Linux (ou WSL) com `gcc` e a biblioteca Pthreads (`libpthread`, padrão em
  qualquer distribuição com glibc).
- `make`.

Não há dependências externas além da biblioteca padrão C e Pthreads.

## Estrutura do projeto

```
Traffic-simulator/
├── src/                  // código-fonte principal
│   ├── main.c            // ponto de entrada da simulação
│   ├── mapa.c            // funções para criar e gerenciar o mapa
│   ├── veiculos.c        // lógica dos carros e ambulância
│   ├── relogio.c         // controle do tempo (ticks)
│   ├── semaforos.c       // controle dos sinais de trânsito
│   └── sincronizacao.c   // mutex, semáforos, variáveis de condição
│
├── include/              // cabeçalhos (interfaces)
│   ├── mapa.h
│   ├── veiculos.h
│   ├── relogio.h
│   ├── semaforos.h
│   └── sincronizacao.h
│
├── docs/                 // documentação
│   ├── relatorio.md      // explicação técnica
│   └── planejamento.md   // divisão de tarefas e decisões
│
├── README.md             // este arquivo
└── Makefile
```

## Compilação

Na raiz do projeto:

```bash
make
```

Isso gera o binário `traffic-simulator` na raiz do projeto. O Makefile usa
`-Wall -Wextra -std=c11 -pthread`, então qualquer warning de compilação
indica algo que deve ser revisado.

Para limpar os artefatos de build:

```bash
make clean
```

## Execução

```bash
./traffic-simulator
```

ou, em um único passo:

```bash
make run
```

A simulação roda por **240 ticks** (configurável em `DURACAO_SIMULACAO_TICKS`
em `src/main.c`), cada tick durando 250ms (configurável em
`DURACAO_TICK_MS`), totalizando cerca de 1 minuto de execução. A cada tick a
tela é limpa e redesenhada (sequência ANSI `\033[H\033[J`), mostrando:

- a malha viária (ruas, cruzamentos, paredes);
- o estado dos semáforos em cada cruzamento (`+` = verde no eixo
  Norte-Sul, `x` = vermelho no eixo Norte-Sul);
- a posição de cada veículo (`#`);
- uma lista textual abaixo do mapa com id, tipo, posição, direção e
  velocidade de cada veículo ainda ativo.

Ao final da simulação, o programa imprime quantos ticks decorreram e
encerra normalmente (todas as threads são finalizadas com `pthread_join`
antes do `return 0`, sem vazamento de threads).

### Ajustando parâmetros da simulação

No início de `src/main.c`:

| Constante | Significado |
|---|---|
| `NUM_CARROS_COMUNS` | quantidade de carros comuns (padrão 14) |
| `NUM_AMBULANCIAS` | quantidade de ambulâncias (padrão 1) |
| `DURACAO_TICK_MS` | duração de cada tick em milissegundos |
| `PERIODO_TROCA_SINAL` | a cada quantos "pulsos" de 100ms o controlador de semáforos alterna os sinais automaticamente |
| `DURACAO_SIMULACAO_TICKS` | quantos ticks a simulação roda antes de encerrar |

O enunciado exige entre 10 e 20 carros simultâneos; o padrão (14 carros +
1 ambulância = 15 threads de veículo) atende a esse intervalo.

## Verificação de corretude (ThreadSanitizer)

O projeto foi validado também com o ThreadSanitizer do GCC, que detecta
condições de corrida (data races) em tempo de execução:

```bash
gcc -Wall -Wextra -std=c11 -Iinclude -pthread -g -fsanitize=thread \
    -o traffic-simulator-tsan src/*.c
./traffic-simulator-tsan
```

Em múltiplas execuções de validação durante o desenvolvimento, a versão
final não apresentou nenhum warning do ThreadSanitizer e finalizou
corretamente (todas as 15 threads de veículo + relógio + semáforos +
visualização encerradas via `pthread_join`, sem deadlock).

## Documentação adicional

- [`docs/relatorio.md`](docs/relatorio.md): explica as decisões de
  implementação (mapa, threads, mecanismos de sincronização, ausência de
  espera ocupada e estratégia contra deadlock), incluindo os bugs
  encontrados durante o desenvolvimento e como foram corrigidos.
- [`docs/planejamento.md`](docs/planejamento.md): divisão de tarefas entre
  os integrantes da equipe e principais decisões de planejamento.

## Integrantes e responsabilidades

> Preencher com os nomes reais da equipe antes da entrega — ver
> `docs/planejamento.md` para o template de divisão de responsabilidades
> por módulo.
