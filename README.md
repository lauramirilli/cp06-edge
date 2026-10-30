# Monitoramento da Adega — Vinheria Agnello

Projeto de IoT que monitora as condições da adega da Vinheria Agnello. Um ESP32 lê **temperatura**, **umidade** e **luminosidade**, marca cada leitura com **data e hora** e envia os dados via **Wi-Fi** e **MQTT** para a plataforma **FIWARE**, onde ficam acessíveis no padrão **NGSI**. As leituras também são recebidas pelo **Node-RED**, que guarda o histórico e alimenta um **dashboard** web com os valores atuais e um gráfico.

> Vinho é sensível a calor, umidade e luz. Acompanhar essas três variáveis ajuda a preservar a qualidade do estoque.

**Simulação no Wokwi:** [Wokwi](https://wokwi.com/projects/477975475036134401)
**Vídeo de explicação:** [YouTube](https://www.youtube.com/watch?v=jKVmnnXVCiE)

---

## Integrantes

| Nome completo | RM |
|---|---|
| Bruce Li Yan Ting | 569168 |
| Felipe Rorato Costa | 570419 |
| Giovanna Andrade Parejas | 569041 |
| Laura Cardin Mirilli | 570841 |

---

## Arquitetura

```
DHT22 + LDR → ESP32 → Wi-Fi → Broker MQTT → IoT Agent UltraLight → Orion Context Broker (NGSI)
                                    │
                                    ├──→ MyMQTT (leitura e envio de comandos)
                                    │
                                    └──→ Node-RED → historico_adega.csv
                                             │
                                             └──→ /Historico → Dashboard (index.html)
```

- O **ESP32** publica as leituras no tópico de telemetria.
- O **IoT Agent** do FIWARE recebe as mensagens e as repassa ao **Orion**, que expõe os dados pela API NGSI.
- O **MyMQTT** assina o mesmo tópico para acompanhar as leituras e publica comandos para o ESP32.
- O **Node-RED** também assina o tópico, salva cada leitura num arquivo CSV (histórico) e disponibiliza esse histórico no endereço `/Historico`.
- O **dashboard** (`index.html`) consulta o Node-RED a cada 5 segundos e mostra a leitura mais recente e um gráfico com o histórico.

---

## Componentes

| Componente | Função | Pino no ESP32 |
|---|---|---|
| ESP32 DevKit | Microcontrolador com Wi-Fi | — |
| DHT22 | Temperatura (°C) e umidade (%) | GPIO 4 (dados) |
| Módulo LDR (fotoresistor) | Luminosidade (%) | GPIO 34 (saída AO) |

Os dois sensores são alimentados pelo 3V3 e pelo GND do ESP32.

---

## Arquivos do repositório

| Arquivo | Descrição |
|---|---|
| `sketch.ino` | Código do ESP32, comentado |
| `diagram.json` | Circuito montado no Wokwi |
| `libraries.txt` | Bibliotecas usadas pelo Wokwi |
| `fluxo_node_red.json` | Fluxo do Node-RED (leitura, armazenamento e exibição) |
| `index.html` | Dashboard web com os valores atuais e o gráfico do histórico |
| `README.md` | Este documento |

---

## Como replicar

### 1. Montar o circuito no Wokwi

1. Acesse [wokwi.com](https://wokwi.com) e crie um projeto **ESP32 (Arduino)**.
2. Substitua o conteúdo de `diagram.json` pelo arquivo deste repositório, ou monte o circuito seguindo a tabela de componentes.

### 2. Adicionar as bibliotecas

No Wokwi, crie (ou edite) o arquivo `libraries.txt` com:

```
PubSubClient
DHT sensor library
```

### 3. Colocar o código

Copie o conteúdo de `sketch.ino` para o editor do Wokwi.

### 4. Configurar

No início do código, ajuste se necessário:

```cpp
const char* mqtt_server = "3.135.234.101";              // IP do servidor FIWARE
const char* topico_telemetria = "/TEF/device022/attrs";  // troque 022 pelo seu ID
const char* topico_comando    = "/TEF/device022/cmd";
```

Se trocar o ID do dispositivo, troque também no comando dentro da função `callback` (`device022@read|`).

### 5. Rodar a simulação

Clique em **Start**. No Serial Monitor deve aparecer:

```
WiFi Conectado!
Hora sincronizada!
Conectando ao MQTT...Conectado!
Publicado: t|24.0|h|40.0|l|94|ts|2026-10-19T17:37:23Z
```

Para testar, clique no DHT22 ou no LDR durante a simulação e altere os valores. A mudança aparece na próxima leitura.

---

## Formato das mensagens

As leituras são enviadas **a cada 5 segundos** no formato **UltraLight 2.0**:

```
t|24.0|h|40.0|l|94|ts|2026-10-19T17:37:23Z
```

| Atributo | Significado | Unidade |
|---|---|---|
| `t` | Temperatura | °C |
| `h` | Umidade relativa | % |
| `l` | Luminosidade | % (0 = escuro, 100 = claro) |
| `ts` | Data e hora da leitura | ISO 8601, em UTC |

O timestamp vem de um servidor **NTP** (`pool.ntp.org`), consultado quando o ESP32 liga.

---

## Leitura e escrita pelo MyMQTT

1. Instale o **MyMQTT** (Android ou iOS).
2. Conecte em:
   - **Broker:** `3.135.234.101`
   - **Porta:** `1883`
3. **Leitura:** em *Subscribe*, assine `/TEF/device022/attrs`. As leituras do ESP32 aparecem a cada 5 segundos.
4. **Escrita:** em *Publish*, envie para o tópico `/TEF/device022/cmd` a mensagem:

   ```
   device022@read|
   ```

   O ESP32 faz uma leitura imediata, sem esperar o intervalo. O Serial Monitor mostra:

   ```
   Comando recebido no topico /TEF/device022/cmd: device022@read|
   Ação: leitura imediata solicitada
   ```

---

## Node-RED: leitura e armazenamento do histórico

O Node-RED roda no computador e faz três coisas:

| Etapa | Blocos | O que faz |
|---|---|---|
| **Leitura** | `mqtt in` → `csv` | Assina `/TEF/device022/attrs` e separa a mensagem em temperatura, umidade, luminosidade e timestamp |
| **Armazenamento** | `csv` → `file` | Converte cada leitura numa linha e acrescenta ao arquivo `historico_adega.csv` |
| **Exibição** | `http in` → `file in` → `csv` → `http response` | Cria o endereço `http://localhost:1880/Historico`, que devolve todo o histórico em JSON |

Um bloco `inject` cria o arquivo vazio quando o Node-RED inicia, para o endereço `/Historico` responder mesmo antes da primeira leitura.

### Como rodar

1. Instale o [Node.js](https://nodejs.org) e depois o Node-RED:

   ```
   npm install -g node-red
   ```

2. Inicie o Node-RED:

   ```
   node-red
   ```

3. Acesse `http://localhost:1880`.
4. No menu ☰ (canto superior direito), clique em **Import**, selecione `fluxo_node_red.json` e clique em **Import**.
5. Clique em **Deploy**. O bloco **Leituras do ESP32** deve mostrar **connected**.
6. Com a simulação do Wokwi rodando, as leituras aparecem na aba **debug** (ícone de inseto) a cada 5 segundos.

### Onde fica o histórico

O arquivo `historico_adega.csv` é criado na pasta onde o comando `node-red` foi executado. Cada linha é uma leitura:

```
2026-10-19T17:37:23Z,24,40,94
```

Formato: `timestamp,temperatura,umidade,luminosidade`. Como os dados ficam em arquivo, o histórico continua disponível mesmo depois de reiniciar o Node-RED.

Para conferir o endereço de exibição, abra `http://localhost:1880/Historico` no navegador. Deve aparecer uma lista assim:

```json
[{"timestamp":"2026-10-19T17:37:23Z","temperatura":24,"umidade":40,"luminosidade":94}]
```

---

## Dashboard

O `index.html` é a página de visualização. Ela mostra:

- **Cards** com a temperatura, a umidade e a luminosidade mais recentes, e o horário da leitura;
- **Gráfico de linhas** com as últimas 100 leituras das três variáveis.

A página consulta `http://localhost:1880/Historico` a cada 5 segundos e se atualiza sozinha. Os horários são convertidos para o fuso local pelo navegador.

### Como abrir

1. Deixe o Node-RED rodando, com o fluxo importado.
2. Deixe a simulação do Wokwi rodando.
3. Abra o `index.html` no navegador, com um duplo clique.

O gráfico usa a biblioteca [Chart.js](https://www.chartjs.org), carregada pela internet; não é preciso instalar nada.

---

## Problemas comuns

| Mensagem no Serial Monitor | Causa provável |
|---|---|
| `Falha, rc=-2` | O servidor MQTT não está acessível: está desligado, o IP está errado ou a porta 1883 está bloqueada. |
| `Falha ao ler o DHT!` | O DHT22 está mal conectado; confira o pino de dados (GPIO 4). |
| Fica parado em `Sincronizando hora...` | Sem acesso à internet; confira se a rede é `Wokwi-GUEST`. |
| Luminosidade invertida | Confira se a saída **AO** do LDR está no GPIO 34. |

| Situação no Node-RED / dashboard | Causa provável |
|---|---|
| Bloco **Leituras do ESP32** sem `connected` | IP do broker errado ou servidor desligado. Confira o broker em `3.135.234.101:1883`. |
| Nenhuma leitura nova no debug | O Wokwi pausa a simulação quando a aba fica em segundo plano. Deixe a aba do Wokwi visível. |
| Erro `ENOENT: no such file` | O arquivo do histórico ainda não existe. Clique em **Deploy** de novo; o bloco **Ao iniciar** cria o arquivo. |
| Dashboard com `--` e gráfico vazio | O Node-RED não está rodando ou ainda não recebeu leituras. Teste `http://localhost:1880/Historico` no navegador. |

---

## Tecnologias

ESP32 · Arduino (C++) · Wokwi · MQTT · FIWARE (IoT Agent UltraLight, Orion Context Broker) · NGSI · MyMQTT · Node-RED · HTML/JavaScript · Chart.js
