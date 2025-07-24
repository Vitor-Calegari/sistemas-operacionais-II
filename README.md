# INE5424 - Sistemas Operacionais II

Este repositório é dedicado ao projeto desenvolvido ao longo da disciplina **Sistemas Operacionais II** (INE5424). O projeto consiste na implementação de uma pilha de protocolo para comunicação entre componentes de veículos autônomos, onde os veículos são abstraídos como **processos** e seus componentes como **threads**.

---

## Testes

Os testes da biblioteca podem ser executados ao utilizar o seguinte comando no diretório raiz do projeto:

```
sudo make
```

---

## Entrega 1 — Comunicação entre Sistemas e seus Componentes

Nesta entrega, foi definida a arquitetura da API e implementada a comunicação básica entre processos utilizando uma engine de **raw sockets**. A arquitetura é composta pelos seguintes módulos:

- **Communicator**: Interface utilizada pelo usuário da API;
- **Protocol**: Multiplexa múltiplos Communicators;
- **NIC**: Multiplexa Protocols e encapsula uma Engine.

Mais informações estão disponíveis em [`Apresentacao_P1`](doc/Apresentacao_P1.pdf).

---

## Entrega 2 — Comunicação entre Componentes de um Mesmo Sistema Autônomo

Nesta etapa, foi adicionada uma nova engine baseada em **memória compartilhada**, permitindo comunicação eficiente entre componentes de um mesmo veículo.

Detalhes adicionais estão em [`Apresentacao_P2`](doc/Apresentacao_P2.pdf).

---

## Entrega 3 — Time-Triggered Publish-Subscribe Messages

Foi introduzida uma nova camada na API para comunicação baseada em **mensagens periódicas**. Essa camada, chamada **SmartData**, é responsável por toda a sincronização necessária entre os componentes para a troca dessas mensagens.

Mais informações em [`Apresentacao_P3`](doc/Apresentacao_P3.pdf).

---

## Entrega 4 — Sincronização Temporal

Implementação de **sincronização temporal** por meio de um protocolo similar ao **PTP (Precision Time Protocol)**, ativado sob demanda.

Detalhes disponíveis em [`Apresentacao_P4`](doc/Apresentacao_P4.pdf).

---

## Entrega 5 — Comunicação Segura em Grupos

Introdução do uso de **Message Authentication Codes (MACs)** nas mensagens, com o objetivo de garantir a **integridade da comunicação** entre componentes.

Mais informações em [`Apresentacao_P5`](doc/Apresentacao_P5.pdf).

---

## Entrega 6 — Comunicação Local Otimizada

A partir desta entrega, componentes de um mesmo veículo não arcam com o custo adicional de comunicação segura ou sincronização temporal, resultando em uma **otimização da comunicação local**.

Mais informações em [`Apresentacao_P6`](doc/Apresentacao_P6.pdf).

---

## Entrega 7 — Mobilidade e Simulação Realista

Nesta entrega, foi adicionado suporte à **mobilidade dos veículos**, e os testes passaram a utilizar **dados e tempos reais**. Também foi realizada uma **análise de desempenho** utilizando as ferramentas `strace` e `perf`, com o objetivo de entender as latências observadas.

Mais informações em [`Apresentacao_P7`](doc/Apresentacao_P7.pdf).

