# Relatório Técnico — Simulador de Tráfego Urbano

Disciplina: Sistemas Operacionais — Concorrência, Sincronização e Deadlocks
Projeto: Traffic-simulator

## 1. Visão geral

O simulador representa uma malha urbana com **16 cruzamentos** (4 linhas x
4 colunas), acima do mínimo de 8 exigido. Cada veículo é uma thread
Pthreads que se move célula a célula sobre essa malha, respeitando
semáforos, mão de direção das vias e exclusão mútua de ocupação de
células. Há **14 carros comuns** (com velocidades rápida, média e lenta
distribuídas ciclicamente) e **1 ambulância** com prioridade de
passagem em cruzamentos, totalizando 15 threads de veículo, dentro do
intervalo de 10 a 20 exigido pelo enunciado.

## 2. O mapa

### 2.1 Origem e adaptação do `Mapa.txt`

O mapa fornecido pela equipe (`Mapa.txt`) descreve uma malha lógica de 4
linhas de cruzamento por 4 colunas:

```
   v   v   v   v
   |   |   |   |
---+---+---+---+---   <- via dupla horizontal
   |   |   |   |
>>>+>>>+>>>+>>>+>>>   <- via de mão única (sentido ->)
   |   |   |   |
---+---+---+---+---   <- via dupla horizontal
   |   |   |   |
>>>+>>>+>>>+>>>+>>>   <- via de mão única (sentido ->)
   |   |   |   |
   ^   ^   ^   ^
```

Essa estrutura foi implementada literalmente em `mapa.c` /
`semaforos.c`:

- **Linhas de cruzamento pares (0 e 2)**: vias horizontais de **mão
  dupla** (`VIA_DUPLA_HORIZONTAL`).
- **Linhas de cruzamento ímpares (1 e 3)**: vias horizontais de **mão
  única**, sentido Leste (`VIA_UNICA_HORIZONTAL`), atendendo ao
  requisito de existir ao menos uma via de mão única.
- **Todas as colunas de cruzamento**: vias verticais de **mão dupla**
  (sentido Norte↔Sul), correspondendo às setas `v` (entrada pelo topo) e
  `^` (saída pela base) do `Mapa.txt`.
- **16 cruzamentos** no total (4×4), folgadamente acima do mínimo de 8.

A função `semaforos_tipo_via_horizontal(linha_cruz)` centraliza essa
regra (par = dupla, ímpar = única) e é consultada tanto pela lógica de
movimento dos veículos (`veiculos.c`, função `direcao_permitida_na_linha`)
quanto, implicitamente, pela forma como os sinais são geridos por eixo.

### 2.2 Representação em matriz

O mapa é uma matriz `Mapa.celulas[MAPA_LINHAS][MAPA_COLS]`
(`include/mapa.h`). Cada cruzamento ocupa 1 célula; entre dois
cruzamentos consecutivos há um trecho de `TAM_TRECHO = 3` células de rua.
Isso gera uma matriz de 17×17 células, onde cada célula tem um tipo
(`CELULA_RUA`, `CELULA_CRUZAMENTO` ou `CELULA_PAREDE` para áreas fora da
malha viária) e um mutex próprio que protege seu estado de ocupação
(`ocupada`, `ocupante_id`).

As vias não são cruzes isoladas: `mapa_inicializar` conecta os
cruzamentos em sequência ao longo de toda a linha/coluna, formando ruas
contínuas que atravessam todos os 4 cruzamentos de cada linha/coluna, com
trechos de entrada/saída nas extremidades do mapa.

## 3. Threads do sistema

| Thread | Função | Arquivo |
|---|---|---|
| Relógio global | avança o tick discreto e acorda as threads de veículo a cada tick | `relogio.c` |
| Controlador de semáforos | alterna os sinais periodicamente e concede prioridade à ambulância | `semaforos.c` |
| Cada carro (14x) | thread independente que se move pela malha | `veiculos.c` |
| Ambulância (1x) | thread igual à dos carros, mas com solicitação de prioridade | `veiculos.c` |
| Visualização | redesenha o terminal a cada tick | `main.c` |

Todas as threads de veículo são criadas com `pthread_create` e
sincronizadas no encerramento com `pthread_join` (ver `main.c`), conforme
exigido.

## 4. Mecanismos de sincronização e onde foram usados

| Mecanismo | Onde | Para quê |
|---|---|---|
| **Mutex por célula** (`Celula.mutex`) | `mapa.c`, usado em `mapa_tentar_ocupar`, `mapa_liberar` e em `tentar_mover` (`veiculos.c`) | Garante exclusão mútua na ocupação de uma célula: dois veículos nunca conseguem marcar a mesma célula como ocupada simultaneamente. |
| **Mutex por cruzamento** (`Cruzamento.mutex`) | `semaforos.c` | Protege o estado do sinal (cor de cada eixo, flags de prioridade de ambulância) contra leitura/escrita concorrente. |
| **Mutex global de log** (`g_mutex_log`) | `sincronizacao.c` | Evita que mensagens de eventos (ex.: pedidos de prioridade da ambulância) se intercalem de forma corrompida no terminal quando escritas por threads diferentes. |
| **Mutex de estado por veículo** (`Veiculo.mutex_estado`) | `veiculos.c`, `main.c` (na thread de visualização) | Protege os campos de posição/estado de cada veículo contra leitura concorrente pela thread de visualização enquanto a thread do veículo os atualiza. |
| **Variável de condição do relógio** (`Relogio.cond_tick`) | `relogio.c` | Os veículos e a thread de visualização dormem em `relogio_esperar_proximo_tick` (via `pthread_cond_wait`) até o relógio emitir um `pthread_cond_broadcast` no próximo tick — **sem espera ocupada**. |
| **Variável de condição por cruzamento** (`Cruzamento.cond_sinal`) | `semaforos.c` | Um veículo parado num sinal vermelho dorme em `semaforo_esperar_verde` (via `pthread_cond_wait`) até o controlador de semáforos trocar o sinal e fazer `pthread_cond_broadcast` — **sem espera ocupada**. |
| **Semáforo POSIX por cruzamento** (`sem_t *sem_capacidade`) | `semaforos.c`, inicializado com `CAPACIDADE_CRUZAMENTO = 1` | Limita quantos veículos podem estar simultaneamente **dentro** da célula de cruzamento, mesmo com o sinal verde — uma segunda camada de proteção além do mutex de célula, evitando que duas threads que conseguiram "passar o sinal verde" colidam dentro do cruzamento. |
| **Threads (Pthreads)** | todas as entidades concorrentes | Carros, ambulância, relógio global e controlador de semáforos são todos threads independentes, criadas com `pthread_create` e sincronizadas com `pthread_join`. |

## 5. Ausência de espera ocupada

O enunciado proíbe espera ocupada para carros parados no sinal vermelho
ou aguardando liberação de cruzamento. Isso foi atendido em duas frentes:

1. **Espera pelo tick do relógio**: a função `relogio_esperar_proximo_tick`
   usa `pthread_cond_wait`, que libera o mutex e bloqueia a thread sem
   consumir CPU; ela só retorna quando o relógio chama
   `pthread_cond_broadcast` no próximo tick.
2. **Espera pelo sinal verde**: a função `semaforo_esperar_verde` (em
   `semaforos.c`) também usa `pthread_cond_wait` sobre a variável de
   condição do cruzamento — o carro é acordado apenas quando o sinal de
   fato muda (`trocar_sinal` ou `forcar_verde_para_ambulancia` fazem o
   `pthread_cond_broadcast`), nunca checando a cor do sinal em loop.
3. **Espera pela capacidade do cruzamento**: a função
   `semaforo_entrar_cruzamento` usa `sem_timedwait` em vez de um `while`
   girando sobre `sem_trywait`. O processo dorme bloqueado no kernel
   entre tentativas (não há *busy-waiting* real); o *timeout* curto
   (200ms) serve apenas para permitir que a thread perceba o encerramento
   da simulação, e não para sondar repetidamente em alta frequência.

## 6. Estratégia de prevenção de deadlock

A estratégia escolhida foi a de **ordem fixa de aquisição de recursos**,
implementada em `sincronizacao.c` através das funções `travar_em_ordem` e
`destravar_em_ordem`.

**O problema**: quando um veículo se move, ele precisa manipular dois
mutexes de célula ao mesmo tempo — o da célula de origem (que ele está
deixando) e o da célula de destino (que ele está tentando ocupar). Se
dois veículos tentassem se mover em direções opostas ao mesmo tempo (por
exemplo, V1 de A para B, e V2 de B para A), travar "origem primeiro,
depois destino" de forma incondicional poderia gerar **espera circular**:
V1 trava A e espera por B, enquanto V2 trava B e espera por A — um
deadlock clássico.

**A solução**: cada célula tem um identificador numérico único
(`id = linha * MAPA_COLS + col`). Em vez de travar "origem, depois
destino", o código sempre trava **o mutex de menor id primeiro**,
independentemente de ser a célula de origem ou destino:

```c
void travar_em_ordem(pthread_mutex_t *mutexA, int idA,
                      pthread_mutex_t *mutexB, int idB) {
    if (idA == idB) { pthread_mutex_lock(mutexA); return; }
    if (idA < idB) {
        pthread_mutex_lock(mutexA);
        pthread_mutex_lock(mutexB);
    } else {
        pthread_mutex_lock(mutexB);
        pthread_mutex_lock(mutexA);
    }
}
```

Com essa ordenação global, V1 e V2 do exemplo acima vão **ambos tentar
travar primeiro o mesmo mutex** (o de menor id). Um deles consegue e
prossegue para travar o segundo; o outro bloqueia já na primeira
tentativa, sem nunca chegar a deter um recurso enquanto espera por outro.
Isso elimina a possibilidade de espera circular entre dois veículos
competindo por células adjacentes — a condição necessária para deadlock
nesse cenário é quebrada.

Esse mesmo padrão é aplicado tanto para movimentos comuns entre células
de rua quanto para a entrada num cruzamento (`tentar_mover`, em
`veiculos.c`), garantindo consistência em toda a base de código.

Além disso, a ordem de aquisição de recursos para entrar num cruzamento
segue sempre a mesma sequência em todas as threads: **(1)** esperar sinal
verde → **(2)** obter vaga de capacidade do cruzamento → **(3)** travar
mutexes de célula em ordem fixa. Como todas as threads de veículo seguem
exatamente essa mesma ordem de aquisição de recursos, não há
possibilidade de uma thread tentar adquirir o semáforo de capacidade
enquanto já detém um mutex de célula que outra thread está esperando (o
que poderia gerar outra forma de espera circular envolvendo tipos
diferentes de recurso).

## 7. Prioridade da ambulância

Quando a ambulância (única thread com `tipo == TIPO_AMBULANCIA`) está a
caminho de um cruzamento e o sinal do eixo que ela precisa não está
verde, ela chama `semaforo_solicitar_prioridade_ambulancia`, que marca
uma flag (`ambulancia_solicitando`) no cruzamento, protegida pelo mutex
do cruzamento. A thread controladora de semáforos verifica essa flag
periodicamente e, ao encontrá-la, chama `forcar_verde_para_ambulancia`,
que: (1) trava o mutex do cruzamento, (2) coloca o eixo oposto em
vermelho, (3) coloca o eixo solicitado em verde, (4) faz
`pthread_cond_broadcast` para acordar a ambulância (e qualquer outro
veículo do mesmo eixo). Como essa troca acontece inteiramente sob o
mutex do cruzamento, nenhuma thread observa um estado intermediário
inconsistente (ex.: os dois eixos verdes simultaneamente).

É importante notar que a prioridade da ambulância **não contorna** a
exclusão mútua de células: mesmo com o sinal aberto à força, a
ambulância ainda precisa passar pelo semáforo de capacidade do
cruzamento e pelos mutexes de célula como qualquer outro veículo — a
prioridade apenas acelera quando o sinal abre, nunca permite que duas
threads ocupem a mesma célula. Todo pedido e concessão de prioridade é
registrado via `log_evento` (mutex de log), por exemplo:

```
[AMBULANCIA] Solicitando prioridade no cruzamento (2,2) eixo=NS
[AMBULANCIA] Prioridade concedida no cruzamento (2,2) eixo=NS
```

## 8. Bugs encontrados e correções realizadas

Durante o desenvolvimento e teste do projeto, os seguintes problemas
foram identificados e corrigidos antes da versão final:

### 8.1 Thread presa indefinidamente no encerramento (bug crítico)

**Sintoma**: em testes de execução completa, uma das 15 threads de
veículo (`C07`) nunca finalizava, e o programa ficava bloqueado para
sempre no `pthread_join` final.

**Causa raiz**: a função `semaforo_esperar_verde` bloqueava a thread em
`pthread_cond_wait` observando **apenas** a cor do sinal do cruzamento.
Quando o `main` sinalizava o fim da simulação e a thread controladora de
semáforos parava de rodar, nenhuma outra thread fazia mais
`pthread_cond_broadcast` naquela variável de condição. Qualquer veículo
que estivesse parado num sinal vermelho exatamente nesse instante ficava
bloqueado para sempre, pois a condição de espera nunca mudava e nenhum
broadcast adicional chegava.

**Correção**: 
1. `semaforo_esperar_verde` passou a checar também a flag global
   `g_semaforos_encerrar` em sua condição de loop, retornando mesmo sem o
   sinal estar verde caso a simulação esteja sendo encerrada.
2. Foi criada a função `semaforos_broadcast_encerramento`, chamada pelo
   `main` imediatamente após sinalizar o fim da simulação, que percorre
   todos os cruzamentos e dá um `pthread_cond_broadcast` final em cada um,
   garantindo que nenhuma thread fique presa em `pthread_cond_wait`.
3. Por simetria, `semaforo_entrar_cruzamento` (que aguardava o semáforo
   de capacidade do cruzamento) deixou de usar `sem_wait` puro (bloqueio
   indefinido) e passou a usar `sem_timedwait` em um laço curto que
   também observa `g_semaforos_encerrar`, eliminando a mesma classe de
   problema para esse recurso.
4. Em `veiculos.c`, o código que trata o movimento por um cruzamento
   passou a verificar o valor de retorno dessas duas funções: se
   indicarem encerramento (e não sinal verde / vaga obtida), o veículo
   simplesmente finaliza sua thread sem tentar concluir o movimento.

Após a correção, **15 execuções completas consecutivas** (build normal e
build instrumentada) finalizaram com sucesso, todos os 15 veículos
reportando finalização exatamente uma vez e o programa retornando ao
prompt normalmente.

### 8.2 Condições de corrida (data races) detectadas via ThreadSanitizer

Para validar a correção da sincronização além de testes manuais, o
projeto foi compilado com `-fsanitize=thread` (ThreadSanitizer) e
executado repetidamente. Foram detectadas e corrigidas duas classes de
data race:

**a) Leitura de `Celula.ocupada` sem proteção na thread de
visualização.** A função `desenhar_celula` (em `main.c`) lia o campo
`ocupada` da célula do mapa diretamente, sem travar o mutex daquela
célula, enquanto as threads de veículo escrevem esse mesmo campo sob o
mutex (em `tentar_mover`/`mapa.c`). Embora a consequência prática fosse
apenas uma exibição eventualmente desatualizada por um instante (não uma
corrupção de estado da simulação), é uma data race real em C, com
comportamento indefinido pela norma da linguagem. **Correção**:
`desenhar_celula` agora trava o mutex da célula antes de ler `ocupada` e
o libera imediatamente após copiar o valor para uma variável local.

**b) Flag global `g_semaforos_encerrar` como `int` comum.** Essa flag é
escrita pelo `main` e lida por várias threads (controlador de semáforos,
threads de veículo) sem qualquer sincronização. **Correção**: o tipo foi
alterado para `atomic_int` (de `<stdatomic.h>`, C11), que garante
operações de leitura/escrita atômicas e reconhecidas como sincronizadas
pelas ferramentas de análise de concorrência, sem o custo de um mutex
completo para uma flag tão simples.

Após essas correções, três execuções consecutivas com ThreadSanitizer
não reportaram nenhum warning de data race, e o programa continuou
finalizando corretamente (15 veículos, exit code 0).

### 8.3 Ajustes de portabilidade

A função `usleep`, usada inicialmente para controlar a duração dos ticks
e o intervalo de verificação do controlador de semáforos, gerava warning
de declaração implícita na glibc do ambiente de testes (Ubuntu 24.04,
glibc 2.39), que não expõe mais `usleep` apenas com `_POSIX_C_SOURCE`
definido. Para manter o código portável e sem warnings, todas as
chamadas a `usleep` foram substituídas por uma função auxiliar local
`dormir_ms`, baseada em `nanosleep` (POSIX, `<time.h>`), que é a forma
recomendada atualmente para pausas com granularidade de milissegundos.

## 9. Validação final

- Compilação com `make` (`-Wall -Wextra -std=c11 -pthread`): **sem
  warnings**.
- 6 execuções completas da build de produção: **6/6 com exit code 0**,
  todas com os 15 veículos finalizando exatamente uma vez.
- 3 execuções completas com ThreadSanitizer: **0 data races
  reportadas** em todas as execuções.
- Prioridade da ambulância observada e registrada em log em todas as
  execuções de teste.
- Nenhuma espera ocupada identificada (toda espera bloqueante usa
  `pthread_cond_wait` ou `sem_timedwait`/`sem_wait`).
