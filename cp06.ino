/*
 * ============================================================
 *  Monitoramento da adega da Vinheria Agnello
 * ============================================================
 *  O ESP32 lê temperatura e umidade (DHT22) e luminosidade (LDR),
 *  marca cada leitura com data e hora (timestamp) e envia tudo
 *  via Wi-Fi, pelo protocolo MQTT, para o back-end FIWARE.
 *
 *  Caminho dos dados:
 *  ESP32 -> Wi-Fi -> Broker MQTT -> IoT Agent (FIWARE) -> Orion (NGSI)
 *
 *  Pelo aplicativo MyMQTT é possível:
 *   - LER as leituras (assinando o tópico de telemetria);
 *   - ESCREVER comandos para o ESP32 (publicando no tópico de comando).
 * ============================================================
 */

#include <WiFi.h>          // Conexão Wi-Fi do ESP32
#include <PubSubClient.h>  // Cliente MQTT (publicar e assinar tópicos)
#include <DHT.h>           // Leitura do sensor DHT22
#include <time.h>          // Data e hora (usado no timestamp)

// ---------- Pinos e sensores ----------
#define DHTPIN 4        // Pino de dados do DHT22
#define DHTTYPE DHT22   // Modelo do sensor
#define LDR_PIN 34      // Pino analógico ligado à saída AO do LDR

// ---------- Rede Wi-Fi ----------
// Rede virtual do Wokwi, que dá acesso à internet ao ESP32 simulado
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ---------- Servidor MQTT (FIWARE) ----------
// IP público do servidor onde roda o broker MQTT do FIWARE
const char* mqtt_server = "3.135.234.101";

// Tópicos MQTT no padrão do IoT Agent do FIWARE: /<apikey>/<id do dispositivo>/...
// "TEF" é a apikey e "device022" é o identificador do nosso dispositivo
const char* topico_telemetria = "/TEF/device022/attrs";  // ESP32 ENVIA as leituras aqui
const char* topico_comando    = "/TEF/device022/cmd";    // ESP32 RECEBE comandos aqui

// ---------- Objetos principais ----------
DHT dht(DHTPIN, DHTTYPE);       // Sensor de temperatura e umidade
WiFiClient espClient;           // Conexão de rede usada pelo MQTT
PubSubClient client(espClient); // Cliente MQTT que usa essa conexão

// ---------- Controle do envio ----------
unsigned long lastMsg = 0;     // Momento do último envio (em milissegundos)
const long interval = 5000;    // Envia uma leitura a cada 5 segundos
bool leituraImediata = false;  // Vira "true" quando chega o comando de leitura pelo MyMQTT


// ============================================================
//  DATA E HORA (TIMESTAMP)
// ============================================================

// Busca a hora certa na internet usando um servidor NTP.
// Sem isso, o ESP32 não sabe que dia é: o relógio interno começa em 1970.
void sincronizarHora()
{
    configTime(0, 0, "pool.ntp.org");  // Fuso 0 = horário UTC (padrão para dados de IoT)
    Serial.print("Sincronizando hora");

    // Espera até o relógio sair de 1970 (1700000000 segundos = novembro de 2023)
    while (time(nullptr) < 1700000000)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nHora sincronizada!");
}

// Gera o timestamp da leitura no formato ISO 8601, por exemplo: 2026-10-19T17:37:23Z
// O "Z" no final indica que o horário está em UTC (3 horas a mais que Brasília).
void gerarTimestamp(char* destino, size_t tamanho)
{
    time_t agora = time(nullptr);   // Hora atual em segundos
    struct tm utc;
    gmtime_r(&agora, &utc);         // Separa em ano, mês, dia, hora...
    strftime(destino, tamanho, "%Y-%m-%dT%H:%M:%SZ", &utc);  // Monta o texto
}


// ============================================================
//  SENSORES
// ============================================================

// Lê o LDR e converte o valor para porcentagem de luminosidade.
int lerLuminosidade()
{
    // O ESP32 lê valores analógicos de 0 a 4095
    int bruto = analogRead(LDR_PIN);

    // No módulo LDR do Wokwi, quanto mais luz, menor o valor lido.
    // Por isso invertemos a escala: 0% = escuro, 100% = muito claro.
    return constrain(map(bruto, 4095, 0, 0, 100), 0, 100);
}

// Lê todos os sensores, monta a mensagem e publica no MQTT.
void publicarLeitura()
{
    float t = dht.readTemperature();  // Temperatura em °C
    float h = dht.readHumidity();     // Umidade relativa em %

    // Se o DHT22 falhar, a leitura volta como "NaN" (não é um número).
    // Nesse caso não enviamos nada, para não mandar dado errado ao servidor.
    if (isnan(t) || isnan(h))
    {
        Serial.println("Falha ao ler o DHT!");
        return;
    }

    int l = lerLuminosidade();  // Luminosidade em %

    char ts[25];
    gerarTimestamp(ts, sizeof(ts));  // Data e hora desta leitura

    // Monta a mensagem no formato UltraLight 2.0, que o IoT Agent do FIWARE entende:
    // pares "atributo|valor" separados por "|"
    // Exemplo: t|24.0|h|40.0|l|94|ts|2026-10-19T17:37:23Z
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "t|%.1f|h|%.1f|l|%d|ts|%s", t, h, l, ts);

    client.publish(topico_telemetria, buffer);  // Envia para o broker
    Serial.printf("Publicado: %s\n", buffer);   // Mostra no Serial Monitor
}


// ============================================================
//  MQTT: RECEBIMENTO DE COMANDOS E CONEXÃO
// ============================================================

// Executada automaticamente sempre que chega uma mensagem no tópico de comando
// (por exemplo, enviada pelo aplicativo MyMQTT).
void callback(char* topic, byte* payload, unsigned int length)
{
    // A mensagem chega como bytes soltos; copiamos para um texto terminado em '\0'
    char msg[length + 1];
    memcpy(msg, payload, length);
    msg[length] = '\0';

    Serial.printf("Comando recebido no topico %s: %s\n", topic, msg);

    // Comando "device022@read|": pede uma leitura imediata, sem esperar o intervalo
    if (strcmp(msg, "device022@read|") == 0)
    {
        leituraImediata = true;
        Serial.println("Ação: leitura imediata solicitada");
    }
    else
    {
        // Qualquer outro comando é ignorado, mas registrado no Serial Monitor
        Serial.println("Ação: Comando não reconhecido. Ignorado.");
    }
}

// Conecta (ou reconecta) ao broker MQTT, tentando de novo até conseguir.
void reconnect()
{
    while (!client.connected())
    {
        Serial.print("Conectando ao MQTT...");

        // Cada cliente MQTT precisa de um ID único; geramos um aleatório
        char clientId[30];
        snprintf(clientId, sizeof(clientId), "ESP32_Wokwi_%04X", random(0xffff));

        if (client.connect(clientId))
        {
            Serial.println("Conectado!");
            client.subscribe(topico_comando);  // Passa a "ouvir" o tópico de comandos
        }
        else
        {
            // client.state() devolve o código do erro (ex.: -2 = servidor inalcançável)
            Serial.print("Falha, rc=");
            Serial.print(client.state());
            Serial.println(" Retentando em 5s...");
            delay(5000);
        }
    }
}


// ============================================================
//  PROGRAMA PRINCIPAL
// ============================================================

// Roda uma única vez, quando o ESP32 liga.
void setup()
{
    Serial.begin(115200);  // Inicia o Serial Monitor
    dht.begin();           // Inicia o DHT22

    // 1) Conecta ao Wi-Fi
    WiFi.begin(ssid, password);
    Serial.print("Conectando WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Conectado!");

    // 2) Acerta o relógio pela internet (necessário para o timestamp)
    sincronizarHora();

    // 3) Configura o servidor MQTT (porta 1883) e a função que trata os comandos
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

// Roda repetidamente, para sempre.
void loop()
{
    // Garante que a conexão MQTT está ativa
    if (!client.connected()) reconnect();

    // Mantém a conexão viva e processa mensagens recebidas (chama o callback)
    client.loop();

    // Publica quando: chegou um comando de leitura OU já passaram 5 segundos.
    // Usamos millis() em vez de delay() para o ESP32 não "travar" e
    // continuar recebendo comandos enquanto espera.
    unsigned long now = millis();
    if (leituraImediata || now - lastMsg > interval)
    {
        lastMsg = now;
        leituraImediata = false;
        publicarLeitura();
    }
}
