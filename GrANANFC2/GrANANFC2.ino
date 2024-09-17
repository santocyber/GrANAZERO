#include <Wire.h>
#include <PN532_I2C.h> // Biblioteca Elechouse
#include <PN532.h>
#include "FS.h"
#include "FFat.h" // FATFS no ESP32
#include <TFT_eSPI.h> // Biblioteca para o display TFT
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Definindo os pinos I2C para o ESP32-S3
#define SDA_PIN 21
#define SCL_PIN 47

// Criando o objeto I2C e o PN532
PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

// Criando o objeto TFT com suporte a toque
TFT_eSPI tft = TFT_eSPI(); // Inicializa com pinos padrão da configuração do TFT_eSPI

// Arquivo de armazenamento FATFS para UIDs
const char* filePath = "/nfc_uids.txt";

// Variáveis para armazenar os dados do cartão NFC
uint8_t uid[7]; // Máximo de 7 bytes para o UID
uint8_t uidLength;
uint8_t cardData[1024]; // Buffer para armazenar os dados do cartão (dependendo do tamanho)

// Variáveis para toque
uint16_t x = 0, y = 0; // Coordenadas de toque brutas

// Estados do sistema
enum State {
  HOME_SCREEN,
  READ_CARD,
  VIEW_CARDS,
  EMULATE_CARD,
  DELETE_CARDS
};

State currentState = HOME_SCREEN;

// Variável para rastrear o tempo de entrada no estado
unsigned long stateStartTime = 0;

// Declaração das funções
bool uidExists(uint8_t *uid, uint8_t uidLength);
void saveCardToFile(uint8_t *uid, uint8_t uidLength);
void listSavedUIDs();
void showHomeScreen();
void readCard();
void displayCardData();
void emulateCard();
void deleteAllCards();
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
bool uidExists(uint8_t *uid, uint8_t uidLength) {
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
void saveCardToFile(uint8_t *uid, uint8_t uidLength) {
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

// Função para listar UIDs salvos e retornar à tela inicial após 10 segundos
// Função para listar UIDs salvos e retornar à tela inicial após 10 segundos
void listSavedUIDs() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
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
    delay(3000);
    showHomeScreen();
    return;
  }

  uint16_t y = 40; // Alteração aqui: int -> uint16_t
  uint16_t index = 0; // Também alterado para uint16_t

  while (file.available() && index < 10) {
    String line = file.readStringUntil('\n');
    tft.setCursor(0, y);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.println(line);
    y += 20;
    index++;
  }

  file.close();

  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (tft.getTouch(&x, &y)) {  // Certifique-se que x e y são uint16_t
      // Apaga todos os cartões
      deleteAllCards();
      break;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  currentState = HOME_SCREEN;
  showHomeScreen();
}


// Função para apagar todos os cartões salvos
void deleteAllCards() {
  if (FFat.remove(filePath)) {
    Serial.println("Todos os cartões foram apagados.");
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 100);
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.println("Cartoes apagados!");
    delay(2000); // Mostra a mensagem por 2 segundos
  } else {
    Serial.println("Erro ao apagar os cartões.");
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 100);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.println("Erro ao apagar!");
    delay(2000); // Mostra a mensagem de erro por 2 segundos
  }
}
void emulateCard() {
       nfc.begin();      // Inicializa o PN532
       nfc.SAMConfig();  // Configura o PN532 para operação normal

      // Libera qualquer operação de leitura passiva antes de iniciar a emulação
      nfc.inRelease();
      delay(100);  // Pequeno atraso para estabilizar

      // Inicia a emulação 
      uint8_t success = nfc.tgInitAsTarget();
        Serial.println(success);

      if (success) {
        Serial.println("Emulação de tag iniciada com sucesso.");
        
        // Exibe na tela que a tag está sendo emulada
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 100);
        tft.setTextColor(TFT_GREEN);
        tft.setTextSize(2);
        tft.println("Cartao emulado!");

        // Mantém a emulação ativa por 30 segundos
        unsigned long emulateStartTime = millis();
        while (millis() - emulateStartTime < 30000) {
          
          // Receber dados do leitor NFC
          uint8_t buffer[255];  // Buffer para receber comandos
          uint8_t responseLength = sizeof(buffer);  // Tamanho do buffer

          // Verifica se o leitor NFC enviou algum comando
          success = nfc.tgGetData(buffer, responseLength);  // Recebe dados
          if (success > 0) {
            Serial.println("Comando recebido do leitor:");
            for (uint8_t i = 0; i < responseLength; i++) {
              Serial.printf("%02X ", buffer[i]);
            }
            Serial.println();

            // Resposta que será enviada de volta ao leitor NFC
            uint8_t response[] = { 0xD5, 0x00, 0x00, 0xFF, 0xFE, 0x01, 0xFE };  // Exemplo de resposta

            // Envia a resposta para o leitor NFC
            success = nfc.tgSetData(response, sizeof(response));  // Envia a resposta
            if (success > 0) {
              Serial.println("Resposta enviada ao leitor.");
            }
          }

          // Se desejar permitir cancelar a emulação ao tocar na tela:
          if (tft.getTouch(&x, &y)) {
            Serial.println("Emulação interrompida pelo usuário.");
            break;
          }

          vTaskDelay(100 / portTICK_PERIOD_MS);  // Pequeno atraso para evitar sobrecarga
        }

        Serial.println("Emulação concluída.");
      } else {
        Serial.println("Falha ao iniciar a emulação de tag.");
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 100);
        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.println("Falha na emulação!");
      }

    
  

 
  // Após a emulação, retorna para a tela inicial
  showHomeScreen();
}


// Função para ler e exibir os dados do cartão
void readCard() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 100);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("Aproxime o cartao...");
  nfc.SAMConfig();


  unsigned long startTime = millis();
  bool cardDetected = false;

  while (millis() - startTime < 10000) { // Espera por 10 segundos
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
      cardDetected = true;
      Serial.println("Cartão NFC detectado!");

      // Exibe o UID na tela
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0, 100);
      tft.setTextColor(TFT_GREEN);
      tft.setTextSize(2);
      tft.println("Cartao detectado!");
      tft.setCursor(0, 150);
      tft.setTextColor(TFT_WHITE);
      tft.println("UID:");
      for (uint8_t i = 0; i < uidLength; i++) {
        tft.printf("%02X ", uid[i]);
      }
      tft.println();

      // Verifica se o cartão já está salvo
      if (uidExists(uid, uidLength)) {
        tft.setTextColor(TFT_RED);
        tft.println("Cartao ja existe!");
        Serial.println("Cartão já existe.");
      } else {
        saveCardToFile(uid, uidLength);
        tft.setTextColor(TFT_GREEN);
        tft.println("Cartao salvo!");
        Serial.println("Cartão salvo.");
      }

      // Tenta ler as mensagens NDEF (usando blocos MIFARE Classic)
      Serial.println("Tentando ler NDEF...");
      if (readNDEFMessage()) {
        Serial.println("NDEF lido com sucesso.");
      } else {
        Serial.println("Erro ao ler NDEF.");
        tft.setTextColor(TFT_RED);
        tft.println("Erro ao ler NDEF.");
      }

      delay(5000); // Mostra os dados por 5 segundos antes de voltar à tela inicial
      break;
    }
  }

  if (!cardDetected) {
    Serial.println("Nenhum cartão detectado.");
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 100);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.println("Nenhum cartao detectado!");
    delay(2000);
  }

  // Volta à tela inicial
  showHomeScreen();
}

// Função para ler e exibir uma mensagem NDEF
bool readNDEFMessage() {
  uint8_t blockBuffer[16];

  // Tentamos ler os blocos 4 a 6, onde a mensagem NDEF pode estar em um MIFARE Classic
  for (int block = 4; block <= 6; block++) {
    if (nfc.mifareclassic_ReadDataBlock(block, blockBuffer)) {
      Serial.printf("Bloco %d: ", block);
      for (int i = 0; i < 16; i++) {
        Serial.printf("%02X ", blockBuffer[i]);
      }
      Serial.println();

      // Exibe a mensagem NDEF na tela
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(0, 200 + (block - 4) * 20);
      tft.setTextSize(1);
      tft.printf("Bloco %d: ", block);
      for (int i = 0; i < 16; i++) {
        tft.printf("%02X ", blockBuffer[i]);
      }
      tft.println();
    } else {
      Serial.printf("Erro ao ler o bloco %d\n", block);
      return false;
    }
  }

  return true;
}

// Função que controla o toque e a navegação pela interface
void touchTask(void *pvParameters) {
  bool touched = false;

  while (true) {
    // Verifica o toque
    if (tft.getTouch(&x, &y)) {
      // Apenas processa o toque se ele não tiver sido processado anteriormente
      if (!touched) {
        touched = true; // Marca que o toque foi processado
        Serial.print("Toque detectado nas coordenadas: x = ");
        Serial.print(x);
        Serial.print(", y = ");
        Serial.println(y);

        // Processa de acordo com o estado atual
        if (currentState == HOME_SCREEN) {
          if (y < tft.height() / 3) {
            currentState = READ_CARD;
            readCard();
          } else if (y < tft.height() * 2 / 3) {
            currentState = VIEW_CARDS;
            listSavedUIDs();
          } else {
            currentState = EMULATE_CARD;
            emulateCard();
          }
        }
      }
    } else {
      // Reinicializa o estado de toque quando não há toque na tela
      touched = false;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS); // Pequeno atraso para evitar sobrecarga
  }
}
// Função para exibir a tela inicial
void showHomeScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.drawLine(0, tft.height() / 3, tft.width(), tft.height() / 3, TFT_WHITE);
  tft.drawLine(0, tft.height() * 2 / 3, tft.width(), tft.height() * 2 / 3, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(tft.width() / 2 - 60, tft.height() / 6);
  tft.println("LER CARTAO");
  tft.setCursor(tft.width() / 2 - 60, tft.height() / 2);
  tft.println("VER CARTOES");
  tft.setCursor(tft.width() / 2 - 60, (tft.height() * 5 / 6));
  tft.println("EMULAR CARTAO");

  // Reset do estado de toque
  x = 0;
  y = 0;
  currentState = HOME_SCREEN;
}
void setup(void) {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Iniciando...");

  if (!initFATFS()) {
    Serial.println("Falha ao inicializar FATFS. Verifique as conexões.");
    while (1);
  }

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Erro ao encontrar o PN532.");
    while (1);
  }
  nfc.SAMConfig();  // Configura o PN532

  showHomeScreen();
  xTaskCreate(touchTask, "Touch Task", 4096, NULL, 1, &touchTaskHandle);
}

void loop(void) {
  // O loop principal permanece vazio
}



// Função para exibir os dados do cartão
void displayCardData() {
  uint8_t blockData[16];

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 20);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("UID:");

  tft.setTextSize(2);
  for (uint8_t i = 0; i < uidLength; i++) {
    tft.printf("0x%02X ", uid[i]);
  }
  tft.println();

  for (uint8_t block = 0; block < 4; block++) {
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
