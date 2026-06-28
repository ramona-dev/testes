# Planejamento — Traffic-simulator

## 1. Divisão de tarefas por módulo

A equipe (≈5 integrantes) pode se organizar em torno dos módulos abaixo,
que já correspondem à separação real do código-fonte. Sugestão de divisão
(substituir pelos nomes reais dos integrantes):

| Módulo | Arquivo(s) | Responsável (sugestão) | Descrição |
|---|---|---|---|
| Mapa | `src/mapa.c`, `include/mapa.h` | Integrante 1 | Estrutura da malha viária, células, tipos de via, ocupação atômica de células |
| Relógio global | `src/relogio.c`, `include/relogio.h` | Integrante 2 | Tick discreto, variável de condição para acordar threads sem espera ocupada |
| Semáforos | `src/semaforos.c`, `include/semaforos.h` | Integrante 3 | Controle de sinal por cruzamento, prioridade de ambulância, capacidade do cruzamento |
| Veículos | `src/veiculos.c`, `include/veiculos.h` | Integrante 4 | Lógica de movimento de carros e ambulância, escolha de direção, integração com mapa/semáforos/relógio |
| Sincronização / Main / Visualização | `src/sincronizacao.c`, `src/main.c` | Integrante 5 | Estratégia anti-deadlock, log thread-safe, criação/encerramento de threads, renderização ASCII |

Documentação (`README.md`, `docs/relatorio.md`, `docs/planejamento.md`) e
testes de integração (execução completa, validação com ThreadSanitizer)
devem ser responsabilidade compartilhada por todos os integrantes, revisados
em conjunto antes da entrega.

## 2. Decisões de design tomadas

- **Mapa**: optou-se por uma malha 4×4 de cruzamentos (16 no total) em vez
  do mínimo de 8, para que o `Mapa.txt` fornecido pudesse ser reproduzido
  fielmente, com 2 linhas de via dupla horizontal e 2 linhas de via única
  horizontal alternadas, e vias verticais de mão dupla.
- **Granularidade do tick**: cada tick do relógio dura 250ms por padrão,
  valor escolhido para que a visualização em terminal seja acompanhável a
  olho nu, sem ser excessivamente lenta para fins de demonstração/avaliação.
- **Capacidade do cruzamento**: foi definida como 1 veículo por vez
  (`CAPACIDADE_CRUZAMENTO`), simulando um cruzamento simples (não uma
  rotatória), o que reforça a exclusão mútua mesmo quando o sinal está
  verde para múltiplos veículos esperando na mesma via.
- **Velocidades**: implementadas como "divisor de tick" — um carro rápido
  tenta se mover a cada tick, médio a cada 2 ticks, lento a cada 4 ticks
  (`if (tick % velocidade != 0) continue;` em `veiculo_thread`).
- **Rota dos veículos**: simplificada para um comportamento "segue em
  frente, e quando bloqueado escolhe outra direção válida" (função
  `escolher_direcao` em `veiculos.c`), respeitando sempre os limites do
  mapa e o sentido das vias de mão única. Isso evita a necessidade de
  pré-calcular rotas completas, mantendo o foco do trabalho nos aspectos de
  concorrência exigidos pelo enunciado.
- **Encerramento gracioso**: toda a simulação roda por um número fixo de
  ticks (configurável) e, ao final, o `main` sinaliza encerramento e
  garante (via broadcasts explícitos) que nenhuma thread fique bloqueada
  indefinidamente — ver seção 8.1 do relatório técnico para detalhes desse
  ponto, que foi a principal dificuldade técnica do projeto.

## 3. Próximos passos antes da entrega

- [ ] Substituir os nomes "Integrante 1..5" pelos nomes reais da equipe.
- [ ] Criar o repositório em
      `https://github.com/ramona-dev/Traffic-simulator` (branch `Map` ou
      `main`, conforme definido pela equipe) e subir o conteúdo desta
      pasta.
- [ ] Fazer commits separados por integrante/módulo, identificando a
      participação de cada um no histórico (`git log --author`).
- [ ] Revisar o `docs/relatorio.md` e complementar com prints/capturas de
      tela da simulação em execução, se o formato de entrega permitir.
- [ ] Testar a compilação em uma máquina limpa (ou container) antes da
      entrega final, para garantir que não há dependências implícitas do
      ambiente de desenvolvimento.
