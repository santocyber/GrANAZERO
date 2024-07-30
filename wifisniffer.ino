#include <WiFi.h>
#include "esp_wifi.h"
#include "TFT_eSPI.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

unsigned long lastPacketTime = 0;
bool promiscuo = false;

QueueHandle_t packetQueue;

wifi_promiscuous_filter_t snifferFilter = {
  .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL
};



void processPacket(const PacketInfo& pktInfo) {
  wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t *)&pktInfo.packet;
  const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)p->payload;
  const wifi_ieee80211_mac_hdr_t *hdr = (wifi_ieee80211_mac_hdr_t *) &ipkt->payload[0];

  char macStr[18];
  snprintf(macStr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", 
           hdr->addr2[0], hdr->addr2[1], hdr->addr2[2], 
           hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);


  Serial.printf("Pacote recebido:\nTipo: %d\nEndereço MAC: %s\nRSSI: %d\nCanal: %d\nTamanho do Pacote: %d bytes\n", 
                pktInfo.type, macStr, p->rx_ctrl.rssi, p->rx_ctrl.channel, p->rx_ctrl.sig_len);


  static int cursorY = 0;  // Variável estática para rastrear a posição do cursor Y
  static int iterationCount = 0;  // Contador de iterações
  
    // Reseta o cursor Y após 4 iterações
  if (iterationCount >= 3) {
    cursorY = 0;
    iterationCount = 0;
    tft.fillScreen(TFT_BLACK);

  }



  tft.setCursor(0, cursorY);
  // Atualiza o cursor Y e o contador de iterações
  cursorY += 180;  // Ajuste conforme necessário para mover o cursor para baixo
  iterationCount++;



  
  
 bool isEncrypted = (hdr->frame_ctrl & 0x0040);
  if (isEncrypted) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  }

  tft.printf("Pacote recebido:\nTipo: %d\n", pktInfo.type);

  // Destaca o endereço MAC em azul
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.printf("Endereço MAC: %s\n", macStr);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  tft.printf("RSSI: %d\nCanal: %d\n", p->rx_ctrl.rssi, p->rx_ctrl.channel);

  // Destaca o tamanho do pacote em amarelo
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.printf("Tamanho do Pacote: %d bytes\n", p->rx_ctrl.sig_len);



    if (isEncrypted) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  }
  //tft.printf("Conteúdo (%d bytes):\n", p->rx_ctrl.sig_len);
  for (int i = 0; i < 200; i++) {
    if (i % 16 == 0) tft.print("\n");
    tft.printf("%02X ", p->payload[i]);
  }
  tft.print("\n");
  

}




void WiFiSniffer() {


  packetQueue = xQueueCreate(10, sizeof(PacketInfo));

  if (packetQueue == NULL) {
    Serial.println("Erro ao criar a fila.");
    return;
  }

  Serial.println("Iniciando WiFi Sniffer...");
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.println("WiFi Sniffer...");

  WiFi.disconnect();
  WiFi.mode(WIFI_MODE_STA);

  esp_err_t ret = esp_wifi_set_promiscuous(true);
  if (ret != ESP_OK) {
    Serial.printf("Erro ao iniciar modo promíscuo: %d\n", ret);
    return; // Sair se houver erro
  } else {
    Serial.println("Modo promíscuo iniciado com sucesso");
  }

  ret = esp_wifi_set_promiscuous_filter(&snifferFilter);
  if (ret != ESP_OK) {
    Serial.printf("Erro ao definir filtro promíscuo: %d\n", ret);
    return; // Sair se houver erro
  } else {
    Serial.println("Filtro promíscuo definido com sucesso");
  }

  ret = esp_wifi_set_promiscuous_rx_cb([](void* buf, wifi_promiscuous_pkt_type_t type) {
    PacketInfo pktInfo;
    pktInfo.packet = *(wifi_promiscuous_pkt_t*)buf;
    pktInfo.type = type;
    if (xQueueSend(packetQueue, &pktInfo, 0) != pdTRUE) {
      //Serial.println("Erro ao enviar pacote para a fila");

    }
  });

  if (ret != ESP_OK) {
    Serial.printf("Erro ao definir callback promíscuo: %d\n", ret);
    return; // Sair se houver erro
  } else {
    Serial.println("Callback promíscuo definido com sucesso");
    promiscuo = true;
  }
}


void loopSPIFF() {

  if (promiscuo){

        // Verifica se a flag de saída foi setada
    if (tft.getTouch(&touchX, &touchY)) {
    esp_wifi_set_promiscuous(false);
    promiscuo = false;
      drawUI();
    }
    
    
  
  PacketInfo pktInfo;
      unsigned long currentTime = millis();

  if (xQueueReceive(packetQueue, &pktInfo, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
      if (currentTime - lastPacketTime >= 1000) {
      lastPacketTime = currentTime;
      processPacket(pktInfo);

    }
  }
}
}
