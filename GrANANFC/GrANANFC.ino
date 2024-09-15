#include <Wire.h>
#include <Adafruit_PN532.h>
#include "FS.h"
#include "FFat.h" // FATFS no ESP32
#include <TFT_eSPI.h> // Biblioteca para o display TFT
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Definindo os pinos I2C para o ESP32-S3
#define SDA_PIN 21
#define SCL_PIN 47

// Definindo o endereço do PN532 (padrão para I2C)
#define PN532_I2C_ADDRESS (0x24)

// Criando o objeto PN532
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

// Criando o objeto TFT com suporte a toque
TFT_eSPI tft = TFT_eSPI(); // Inicializa com pinos padrão da configuração do TFT_eSPI

// Arquivo de armazenamento FATFS para UIDs
const char* filePath = "/nfc_uids.txt";

// Variáveis para armazenar os dados do cartão NFC
uint8_t uid[7]; // Máximo de 7 bytes para o UID
uint8_t uidLength;

// Variáveis para toque
uint16_t x = 0, y = 0; // Coordenadas de toque brutas

// Estados do sistema
enum State {
  HOME_SCREEN,
  READ_CARD,
  VIEW_CARDS
};

State currentState = HOME_SCREEN;

// Variável para rastrear o tempo de entrada no estado
unsigned long stateStartTime = 0;

// Declaração das funções
bool uidExists(uint8_t *uid, uint8_t uidLength); // Corrigido
void saveCardToFile(uint8_t *uid, uint8_t uidLength); // Corrigido
void listSavedUIDs();
void showHomeScreen();
void readCard();
void displayCardData();
void touchTask(void *pvParameters);

// Task handles for FreeRTOS tasks
TaskHandle_t touchTaskHandle;

// Inicializa o FATFS
bool initFATFS() {
  if (!FFat.begin()) {
    Serial.println("Erro ao montar FFat. Tentando formatar...");
    if (!FFat.format()) {
      return false;
    }
    if (!FFat.begin()) {
      Serial.println("Erro ao inicializar FATFS após formatação.");
      return false;
    }
  }
  Serial.println("FATFS inicializado com sucesso.");
  return true;
}

// Função para verificar se o UID já existe no arquivo
bool uidExists(uint8_t *uid, uint8_t uidLength) { // Corrigida
  File file = FFat.open(filePath, FILE_READ);
  if (!file) {
    return false;
  }

  String uidStr = "";
  for (uint8_t i = 0; i < uidLength; i++) {
    uidStr += String(uid[i], HEX);
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.indexOf(uidStr) != -1) {
      file.close();
      return true; // UID já existe
    }
  }
  file.close();
  return false;
}

// Função para salvar o UID no arquivo
void saveCardToFile(uint8_t *uid, uint8_t uidLength) { // Corrigida
  File file = FFat.open(filePath, FILE_APPEND);
  if (file) {
    for (uint8_t i = 0; i < uidLength; i++) {
      file.printf("%02X", uid[i]);
    }
    file.println();
    file.close();
    Serial.println("UID salvo com sucesso.");
  } else {
    Serial.println("Erro ao abrir o arquivo.");
  }
}
// Função para leitura de um cartão NFC
void readCard() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 100);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("Aproxime o cartao para leitura...");

  unsigned long startTime = millis();
  bool cardDetected = false;

  while (millis() - startTime < 10000) { // Espera por 10 segundos
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
      cardDetected = true;
      Serial.println("Cartão NFC detectado!");

      if (uidExists(uid, uidLength)) { // Chamada corrigida
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 100);
        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.println("Cartao ja existe!");
        delay(2000); // Mostra os dados por 2 segundos

        Serial.println("Cartão já existe.");
      } else {
        saveCardToFile(uid, uidLength); // Chamada corrigida

        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 100);
        tft.setTextColor(TFT_GREEN);
        tft.setTextSize(2);
        tft.println("Cartao salvo!");
        delay(2000); // Mostra os dados por 2 segundos

        Serial.println("Cartão salvo com sucesso.");
      }

      displayCardData();
      delay(5000); // Mostra os dados por 5 segundos antes de voltar à tela inicial
      break;
    }
  }

  if (!cardDetected) {
    Serial.println("Nenhum cartão detectado. Voltando para a tela inicial.");
  }

  // Após a leitura, sempre volta para a tela inicial
  currentState = HOME_SCREEN;
  showHomeScreen();
}

// Função para listar UIDs salvos e retornar à tela inicial após 10 segundos
void listSavedUIDs() {
  tft.fillScreen(TFT_BLACK); // Limpa a tela
  tft.setCursor(0, 0); // Começa a escrever no topo
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("Cartoes Salvos:");

  File file = FFat.open(filePath, FILE_READ);
  if (!file) {
    Serial.println("Erro ao abrir o arquivo para leitura.");
    tft.setCursor(0, 60);
    tft.setTextSize(2);
    tft.setTextColor(TFT_RED);
    tft.println("Erro ao ler arquivo!");
    delay(3000); // Mostra a mensagem de erro por 3 segundos antes de voltar à tela inicial
    showHomeScreen();
    return;
  }

  int y = 40; // Posição inicial para os UIDs
  int index = 0; // Índice de controle

  Serial.println("UIDs gravados:");
  while (file.available() && index < 10) { // Mostrar até 10 UIDs na tela
    String line = file.readStringUntil('\n');
    Serial.println(line);

    tft.setCursor(0, y);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.println(line);

    y += 20; // Espaço vertical entre os UIDs
    index++;
  }

  file.close();

  // Exibe os cartões salvos por 10 segundos e volta para a tela inicial
  delay(10000); // Aguarda 10 segundos
  currentState = HOME_SCREEN; // Atualiza o estado para HOME_SCREEN
  showHomeScreen(); // Volta para a tela inicial após o tempo
}

// Função que controla o toque, dividindo a tela em duas partes
void touchTask(void *pvParameters) {
  while (true) {
    if (tft.getTouch(&x, &y)) {
      Serial.print("Toque detectado nas coordenadas: x = ");
      Serial.print(x);
      Serial.print(", y = ");
      Serial.println(y);

      if (currentState == HOME_SCREEN) {
        // Dividir a tela: metade superior (0 a tft.height() / 2) e inferior (tft.height() / 2 a tft.height())
        if (y < tft.height() / 2) {
          Serial.println("Área superior tocada: LER CARTÃO");
          currentState = READ_CARD;
          readCard();
        } else {
          Serial.println("Área inferior tocada: VER CARTÕES");
          currentState = VIEW_CARDS;
          listSavedUIDs();
        }
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS); // Pequeno atraso para evitar múltiplos toques
  }
}

// Função para exibir dados do cartão
void displayCardData() {
  uint8_t blockData[16]; // Um bloco MIFARE clássico tem 16 bytes

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 20);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("UID:");

  // Exibe o UID do cartão
  tft.setTextSize(2);
  for (uint8_t i = 0; i < uidLength; i++) {
    tft.printf("0x%02X ", uid[i]);
  }
  tft.println();

  // Tenta ler os blocos de dados
  for (uint8_t block = 0; block < 4; block++) { // Tentativa de ler 4 blocos (0 a 3)
    if (nfc.mifareclassic_ReadDataBlock(block, blockData)) {
      tft.setTextSize(1);
      tft.printf("Bloco %d: ", block);
      for (uint8_t i = 0; i < 16; i++) {
        tft.printf("%02X ", blockData[i]);
      }
      tft.println();
    } else {
      tft.setTextColor(TFT_RED);
      tft.printf("Erro ao ler o bloco %d\n", block);
    }
  }
}

// Função para exibir a tela inicial com a linha divisória e os textos
void showHomeScreen() {
  // Limpa a tela
  tft.fillScreen(TFT_BLACK);

  // Desenha a linha divisória no meio da tela
  tft.drawLine(0, tft.height() / 2, tft.width(), tft.height() / 2, TFT_WHITE);

  // Define a cor e o tamanho do texto
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  // Exibe "LER CARTÃO" na metade superior
  tft.setCursor(tft.width() / 2 - 60, tft.height() / 4); // Ajusta a posição horizontal e vertical
  tft.println("LER CARTAO");

  // Exibe "VER CARTÕES" na metade inferior
  tft.setCursor(tft.width() / 2 - 60, (tft.height() / 2) + (tft.height() / 4)); // Ajusta a posição
  tft.println("VER CARTOES");
}


void setup(void) {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN); // Inicializa o I2C

  // Inicializando o display TFT
  tft.init();
  tft.setRotation(3); // Modo paisagem
  tft.fillScreen(TFT_BLACK); // Limpa a tela
  tft.setTextColor(TFT_WHITE, TFT_BLACK); // Texto branco com fundo preto
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Iniciando...");

  // Inicializando FATFS
  if (!initFATFS()) {
    Serial.println("Falha ao inicializar FATFS. Verifique as conexões.");
    while (1);
  }

  // Inicializando o PN532
  Serial.println("Iniciando o PN532...");
  if (!nfc.begin()) {
    Serial.println("Erro ao inicializar o PN532. Verifique as conexões.");
    while (1);
  }

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Erro ao encontrar o PN532.");
    while (1);
  }

  Serial.print("Firmware do PN532 encontrado: ");
  Serial.print((versiondata >> 24) & 0xFF, HEX);
  Serial.print(".");
  Serial.print((versiondata >> 16) & 0xFF, HEX);
  Serial.print(".");
  Serial.print((versiondata >> 8) & 0xFF, HEX);
  Serial.println((versiondata & 0xFF, HEX));

  // Inicializa o PN532 como leitor NFC
  nfc.SAMConfig();
  Serial.println("PN532 pronto para ler e gravar tags NFC.");

  showHomeScreen(); // Exibe a tela inicial

  // Criar a tarefa de toque
  xTaskCreate(touchTask, "Touch Task", 4096, NULL, 1, &touchTaskHandle);
}

void loop(void) {
  // O loop principal permanece vazio
}
